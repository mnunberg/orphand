#include "orphand_priv.h"

#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>

#include "contrib/cliopts.h"

#define EMBHT_API static
#define EMBHT_KEY_SIZE sizeof(pid_t)
#define EMBHT_VALUE_SIZE sizeof(void*)

#include "contrib/embht.c"

#define TOPLEVEL_BUCKET_COUNT 4096
#define CHILD_BUCKET_COUNT 64

struct child_info {
    /**
     * time child was spawned
     * (used to differentiate between different iterations)
     */
    time_t ctime;


};

typedef embht_table embht_table;

static
orphand_server Server;

static embht_table *
get_pid_table(pid_t pid, int create)
{
    embht_entry *ent;
    ent = embht_fetchi(Server.ht, pid, 1);
    if (ent) {
        if (ent->u_value.ptr == NULL) {
            assert(create);
            ent->u_value.ptr = embht_make(CHILD_BUCKET_COUNT, 0);
            DEBUG("Created new bucket %p", ent->u_value.ptr);
        } else {
            DEBUG("Have bucket=%p, ht=%p", ent, ent->u_value.ptr);
        }
        return ent->u_value.ptr;
    }
    return NULL;
}

static void
register_child(pid_t parent, pid_t child)
{
    embht_table *ht = get_pid_table(parent, 1);
    embht_entry *ent;
    assert(ht);
    ent = embht_fetchi(ht, child, 1);
    ent->u_value.ptr = (void*)1;
}

static void
unregister_child(pid_t parent, pid_t child)
{
    embht_table *ht = get_pid_table(parent, 0);
    if (!ht) {
        return;
    }
    embht_deletei(ht, child);
}

/**
 * Now we need a nice function to traverse over all children and delete
 * their proper entries..
 */

static void
sweep(void)
{
    embht_iterator parents_iter;
    embht_iterinit(Server.ht, &parents_iter);

    while (embht_iternext(&parents_iter)) {
        embht_table *children_ht;
        embht_entry *children_hb;
        embht_iterator child_iter;

        children_hb = embht_itercur(&parents_iter);
        pid_t parent_pid = children_hb->key.u_kdata.kd32;

        DEBUG("Checking children of %d", parent_pid);
        if (parent_pid < 1 || kill(parent_pid, 0) == 0) {
            continue;
        }

        children_ht = children_hb->u_value.ptr;
        embht_iterinit(children_ht, &child_iter);
        while (embht_iternext(&child_iter)) {
            pid_t child_pid = embht_itercur(&child_iter)->key.u_kdata.kd32;
            if (child_pid < 1) {
                continue;
            }
            INFO("Dead parent %d: Killing %d", parent_pid, child_pid);
            kill(child_pid, Server.default_signum);
        }
        embht_destroy(children_ht);
        embht_iterdel(&parents_iter);
    }
}

static void
get_message_dgram(int sock, orphand_message *out)
{
    ssize_t nr;
    int ii;
    struct msghdr msg = { 0 };
    struct iovec iov[3];
    for ( ii = 0; ii < 3; ii++) {
        iov[ii].iov_len = 4;
    }

    iov[0].iov_base = &out->parent;
    iov[1].iov_base = &out->child;
    iov[2].iov_base = &out->action;

    msg.msg_iov = iov;
    msg.msg_iovlen = 3;

    nr = recvmsg(sock, &msg, 0);
    if (nr == -1 || nr != 12) {
        if (nr == -1) {
            ERROR("Couldn't get message, (recvmsg == -1): %s",
                  strerror(errno));
        } else {
            ERROR("Expected 12 bytes, got %d instead", (int)nr);
        }
        out->parent = 0;
    }
}

static void
process_message(const orphand_message *msg)
{
    if (msg->action == ORPHAND_ACTION_REGISTER) {
        register_child(msg->parent, msg->child);
    } else if (msg->action == ORPHAND_ACTION_UNREGISTER) {
        unregister_child(msg->parent, msg->child);
    } else if (msg->action == ORPHAND_ACTION_PING) {
        ERROR("Ping not yet handled!");
    } else {
        ERROR("Received unknown code %d", msg->action);
    }
}

static int
setup_socket(const char *path)
{
    int status,
        sock = socket(AF_UNIX, SOCK_DGRAM, 0);

    struct sockaddr_un uaddr;

    if (sock == -1) {
        perror("socket");
        return -1;
    }

    memset(&uaddr, 0, sizeof(uaddr));
    uaddr.sun_family = AF_UNIX;
    strcpy(uaddr.sun_path, path);
    status = bind(sock, (struct sockaddr*)&uaddr, sizeof(uaddr));
    if (status == -1) {
        perror("bind");
        close(sock);
        return -1;
    }
    return sock;
}


static void start_orphand(const char *path,
                          int interval)
{
    struct timeval last_sweep;
    int sock, maxfd;
    orphand_message msg;
    unlink(path);
    sock = setup_socket(path);

    if (sock == -1) {
        ERROR("Couldn't setup socket. Exiting..");
        exit(1);
    }

    fd_set origfds, outfds;

    FD_ZERO(&origfds);
    FD_SET(sock, &origfds);

    memset(&last_sweep, 0, sizeof(last_sweep));
    struct timeval tmo = { 0, 0 };
    maxfd = sock + 1;
    while (1) {

        outfds = origfds;
        select(maxfd,
               &outfds,
               NULL,
               NULL,
               &tmo);

        if (FD_ISSET(sock, &outfds)) {
            get_message_dgram(sock, &msg);
            if (msg.parent) {
                INFO("Got message: Parent %"PRIu32", "
                     "Child %"PRIu32", "
                     "Action %"PRIx32,
                       msg.parent, msg.child, msg.action);
            }
            if (msg.parent) {
                process_message(&msg);
            }
        }


        if (!tmo.tv_sec) {
            DEBUG("Time to sweep!");
            sweep();
            tmo.tv_sec = interval;
            tmo.tv_usec = 0;
        }
    }

}

int Orphand_Loglevel = LOGLVL_INFO;

int main(int argc, char **argv)
{
    /**
     * ffs, let's implement our own arg parser. why do all other interfaces
     * have to suck so much
     */

    char *path = NULL;
    int lastidx;

    cliopts_entry entries[] = {
    { 'd', "debug", CLIOPTS_ARGT_INT, &Orphand_Loglevel,
            "debug level (higher is more verbose)" },

    { 'f', "socket", CLIOPTS_ARGT_STRING, &path, "Socket path"},

    { 'i', "interval", CLIOPTS_ARGT_INT, &Server.sweep_interval,
            "Polling interval for orphan processes" },

    { 'S', "signal", CLIOPTS_ARGT_INT, &Server.default_signum,
           "Signal number to send to orphan processes" },

    { 0 }
    };

    Server.default_signum = ORPHAND_DEFAULT_SIGNAL;
    Server.sweep_interval = ORPHAND_DEFAULT_SWEEP_INTERVAL;

    cliopts_parse_options(entries, argc, argv, &lastidx, NULL);

    if (Server.default_signum < 0 || Server.default_signum > 32) {
        fprintf(stderr, "Signal number must be > 0 && < 32\n");
        exit(1);
    }

    if (Server.sweep_interval < 1) {
        fprintf(stderr, "Sweep interval must be >= 1\n");
        exit(1);
    }

    if (!path) {
        path = ORPHAND_DEFAULT_PATH;
    }
    Server.ht = embht_make(TOPLEVEL_BUCKET_COUNT, 0);
    start_orphand(path, Server.sweep_interval);
    return 0;
}
