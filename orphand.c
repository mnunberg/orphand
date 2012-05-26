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

#define TOPLEVEL_BUCKET_COUNT 4096
#define CHILD_BUCKET_COUNT 64

struct child_info {
    /**
     * time child was spawned
     * (used to differentiate between different iterations)
     */
    time_t ctime;


};

static
orphand_server Server;

static hashtable *
get_pid_table(pid_t pid, int create)
{
    hashbucket *bucket;
    bucket = ht_fetch(Server.ht, pid, create);
    if (bucket) {
        if (HB_ptr(bucket) == NULL) {
            assert(create);
            HB_ptr(bucket) = ht_make(CHILD_BUCKET_COUNT);
            DEBUG("Created new bucket %p", bucket);
        } else {
            DEBUG("Have bucket=%p, ht=%p", bucket, HB_ptr(bucket));
        }
        return HB_ptr(bucket);
    }
    return NULL;
}

static void
register_child(pid_t parent, pid_t child)
{
    hashtable *ht = get_pid_table(parent, 1);
    assert(ht);
    ht_store(ht, child, (void*)1);
}

static void
unregister_child(pid_t parent, pid_t child)
{
    hashtable *ht = get_pid_table(parent, 0);
    if (!ht) {
        return;
    }
    ht_delete(ht, child);
}

/**
 * Now we need a nice function to traverse over all children and delete
 * their proper entries..
 */

static void
sweep(void)
{
    ht_iterator parents_iter;
    ht_iterinit(Server.ht, &parents_iter);

    while (ht_iternext(&parents_iter)) {
        hashtable *children_ht;
        hashbucket *children_hb;
        ht_iterator child_iter;

        children_hb = ht_itercur(&parents_iter);
        pid_t parent_pid = children_hb->key;

        DEBUG("Checking children of %d", parent_pid);
        if (parent_pid < 1 || kill(parent_pid, 0) == 0) {
            continue;
        }

        children_ht = HB_ptr(children_hb);
        ht_iterinit(children_ht, &child_iter);
        while (ht_iternext(&child_iter)) {
            pid_t child_pid = ht_iterkey(&child_iter);
            if (child_pid < 1) {
                continue;
            }
            INFO("Dead parent %d: Killing %d", parent_pid, child_pid);
            kill(child_pid, Server.default_signum);
        }

        ht_destroy(children_ht);
        ht_iterdel(&parents_iter);
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
    Server.ht = ht_make(TOPLEVEL_BUCKET_COUNT);
    start_orphand(path, Server.sweep_interval);
    return 0;
}
