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

#include <pthread.h>


#define TOPLEVEL_BUCKET_COUNT 4096
#define CHILD_BUCKET_COUNT 64

static hashtable *global_entries;
static int sweep_interval = 2;
static int default_signum = SIGINT;
#define SIGNAL_TO_SEND default_signum

static hashtable *
get_pid_table(pid_t pid, int create)
{
    hashbucket *bucket;
    bucket = ht_fetch(global_entries, pid, create);
    if (bucket) {
        if (bucket->value == NULL) {
            assert(create);
            bucket->value = ht_make(CHILD_BUCKET_COUNT);
            DEBUG("Created new bucket %p", bucket);
        } else {
            DEBUG("Have bucket=%p, ht=%p", bucket, bucket->value);
        }
        return bucket->value;
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
    ht_iterator parent_iter;
    ht_iterinit(global_entries, &parent_iter);

    while (ht_iternext(&parent_iter)) {
        hashtable *children;
        ht_iterator child_iter;
        pid_t parent_pid = ht_iterkey(&parent_iter);
        DEBUG("Checking children of %d", parent_pid);

        if (parent_pid < 1 || kill(parent_pid, 0) == 0) {
            continue;
        }
        children = ht_iterval(&parent_iter);
        ht_iterinit(children, &child_iter);
        while (ht_iternext(&child_iter)) {
            pid_t child_pid = ht_iterkey(&child_iter);
            if (child_pid < 1) {
                continue;
            }
            INFO("Dead parent %d: Killing %d", parent_pid, child_pid);
            kill(child_pid, SIGNAL_TO_SEND);
        }

        ht_destroy(children);
        ht_iterdel(&parent_iter);
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

    nr = recvmsg(sock, &msg, MSG_WAITALL);
    if (nr == -1) {
        ERROR("Couldn't get message: %s", strerror(errno));
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
    int sock;
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

    while (1) {

        /**
         * figure out how much time we have remaining..
         */

        outfds = origfds;
        select(sock+1,
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

static void
print_help(const char *progname)
{
    fprintf(stderr, "USAGE: %s OPTIONS\n", progname);
    fprintf(stderr, "debug=LEVEL [ specify level. Higher is more verbose ]\n");
    fprintf(stderr, "signal=SIG [ default signal to send to orphans ]\n");
    fprintf(stderr, "path=PATH [ path for unix socket ]\n");

}

int main(int argc, char **argv)
{
    /**
     * ffs, let's implement our own arg parser. why do all other interfaces
     * have to suck so much
     */
    char kbuf[4096] = { 0 };
    char *path = NULL;
    char *emsg = NULL;
    int ii, is_ok = 1;
    if (argc == 2 &&
            (strcmp("--help", argv[1]) == 0 ||
            strcmp("-h", argv[1]) == 0 ||
            strcmp("-?", argv[1]) == 0)) {
        print_help(argv[0]);
        exit(0);
    }

    for (ii = 1; ii < argc; ii++) {
        char *vbuf = NULL;
        strcpy(kbuf, argv[ii]);

        for (vbuf = kbuf; *vbuf; vbuf++) {
            if (*vbuf == '=') {
                *vbuf = '\0';
                vbuf++;
                break;
            }
        }

        if (vbuf == NULL || *vbuf == '\0') {
            emsg = "expected key=value format";
            is_ok = 0;
            break;
        }

        if (strcmp(kbuf, "debug") == 0 ) {
            if (sscanf(vbuf, "%d", &Orphand_Loglevel) != 1) {
                emsg = "Expected level for debugging";
                is_ok = 0;
                break;
            }
        } else if (strcmp(kbuf, "path") == 0) {
            size_t vlen = strlen(vbuf);
            path = malloc(vlen+1);
            path[vlen] = 0;
            strcpy(path, vbuf);

        } else if (strcmp(kbuf, "signal") == 0) {
            if (sscanf(vbuf, "%d", &default_signum) != 1) {
                emsg = "signal= need a signal";
                is_ok = 0;
                break;
            }
            if (default_signum < 0 || default_signum > 32) {
                emsg = "Invalid signal. Must be 0 < SIGNAL < 32";
                is_ok = 0;
                break;
            }
        } else {
            emsg = "Unrecognized option";
            is_ok = 0;
            break;
        }
    }

    if (!is_ok) {
        fprintf(stderr,
                "Error parsing options (at %s): %s\n", kbuf, emsg);
        print_help(argv[0]);
        exit(1);

    }

    if (!path) {
        path = "/tmp/orphand.sock";
    }
    global_entries = ht_make(TOPLEVEL_BUCKET_COUNT);
    start_orphand(path, sweep_interval);
    return 0;
}
