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
#include <procstat.h>
#include <fcntl.h>
#include <sys/file.h>

#include <contrib/cliopts.h>

static struct procstat Orphand_pst_dummy;

static int Orphand_Use_Procfs = 1;


#include <contrib/embht.c>

#define TOPLEVEL_BUCKET_COUNT 4096
#define CHILD_BUCKET_COUNT 64

typedef embht_table embht_table;

static
orphand_server Server;

static embht_table *
get_pid_table(pid_t pid, int create)
{
    embht_entry *ent;
    ent = embht_fetchi(Server.ht, pid, create);
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
    struct procstat pstb;

    assert(ht);

    if ( procstat(child, &pstb) != 0 ) {
        fprintf(stderr, "Orphand: procstat(%d) failed with %d,%d\n",
                child, pstb.lib_error, pstb.sys_error);
        return;
    }

    ent = embht_fetchi(ht, child, 1);
    *(uint64_t*)(ent->u_value.value) = pstb.pst_starttime;
}

static void
unregister_child(pid_t parent, pid_t child)
{
    embht_table *ht = get_pid_table(parent, 0);
    if (!ht) {
        return;
    }
    DEBUG("Unregistering %d", child);
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
        struct procstat pstb;

        children_hb = embht_itercur(&parents_iter);
        pid_t parent_pid = children_hb->key.u_kdata.kd32;
        children_ht = children_hb->u_value.ptr;

        DEBUG("Checking children of %d", parent_pid);

        if (parent_pid < 1) {
            ERROR("Found a parent with a PID < 1");
            goto GT_CLEAN_PARENT;
        }

        if (kill(parent_pid, 0) == 0) {
            DEBUG("Parent still alive");
            continue;
        } else {
            int old_errno = errno;
            if (old_errno != ESRCH) {
                WARN("Couldn't determine whether %d is alive: %s",
                     parent_pid,
                     strerror(old_errno));
                goto GT_CLEAN_PARENT;
            }
        }

        embht_iterinit(children_ht, &child_iter);
        while (embht_iternext(&child_iter)) {

            uint64_t child_start =
                    *(uint64_t*)(embht_itercur(&child_iter)->u_value.value);

            pid_t child_pid = embht_itercur(&child_iter)->key.u_kdata.kd32;

            if (child_pid < 1) {
                continue;
            }

            if (procstat(child_pid, &pstb) != 0) {
                fprintf(stderr, "procstat(%d) (%d,%d)\n",
                        child_pid, pstb.lib_error, pstb.sys_error);
                continue;
            }

            if (pstb.pst_starttime != child_start) {
                INFO("PID %d found but start times differ", child_pid);
                continue;
            }

            INFO("Dead parent %d: Killing %d", parent_pid, child_pid);
            kill(child_pid, Server.default_signum);
        }

        GT_CLEAN_PARENT:
        if (children_ht) {
            embht_destroy(children_ht);
        }
        embht_iterdel(&parents_iter);
    }
}

void
orphand_process_message(orphand_server *srv,
                        orphand_client *cli,
                        const orphand_message *msg)
{
    INFO("Sock: %d, Action=%d, Parent=%d, Child=%d",
          cli->sockfd,
          msg->action,
          msg->parent,
          msg->child);
    if (msg->action == ORPHAND_ACTION_REGISTER) {
        register_child(msg->parent, msg->child);
    } else if (msg->action == ORPHAND_ACTION_UNREGISTER) {
        unregister_child(msg->parent, msg->child);
    } else if (msg->action == ORPHAND_ACTION_PING) {

        uint32_t *reply =
                (uint32_t*)((char*)cli->sndbuf.buf + cli->sndbuf.used);
        if (cli->sndbuf.total - cli->sndbuf.used < 12) {
            ERROR("Too little space in send buffer..");
            return;
        }

        reply[0] = msg->parent;
        reply[1] = msg->child;
        reply[2] = msg->action;
        cli->sndbuf.used += 12;

    } else {
        ERROR("Received unknown code %d", msg->action);
        ERROR("A=%d,P=%d,C=%d",
              msg->action,
              msg->parent,
              msg->child);
    }
}


static void start_orphand(const char *path,
                          int interval)
{
    struct timeval last_sweep;

    if (orphand_io_init(&Server, path) == -1) {
        ERROR("Couldn't setup socket. Exiting");
    }


    memset(&last_sweep, 0, sizeof(last_sweep));
    memset(&Server.tmo, 0, sizeof(&Server.tmo));

    /* Ignore SIGPIPE */
    signal(SIGPIPE, SIG_IGN);

    while (1) {
        orphand_io_iteronce(&Server);

        if (!Server.tmo.tv_sec) {
            DEBUG("Time to sweep!");
            sweep();
            Server.tmo.tv_sec = interval;
            Server.tmo.tv_usec = 0;
        }
    }

}

int Orphand_Loglevel = LOGLVL_INFO;

static int Orphand_Lockfd;

int main(int argc, char **argv)
{
    /**
     * ffs, let's implement our own arg parser. why do all other interfaces
     * have to suck so much
     */

    char *path = NULL;
    char *lockfile = NULL;
    int lastidx;

    cliopts_entry entries[] = {
    { 'd', "debug", CLIOPTS_ARGT_INT, &Orphand_Loglevel,
            "debug level (higher is more verbose)" },

    { 'f', "socket", CLIOPTS_ARGT_STRING, &path, "Socket path"},

    { 'i', "interval", CLIOPTS_ARGT_INT, &Server.sweep_interval,
            "Polling interval for orphan processes" },
    { 'l', "lockfile", CLIOPTS_ARGT_STRING, &lockfile,
            "Lockfile to use"},
    { 'S', "signal", CLIOPTS_ARGT_INT, &Server.default_signum,
           "Signal number to send to orphan processes" },
    { 0,   "no-procfs", CLIOPTS_ARGT_INT, &Orphand_Use_Procfs,
            "Don't check procfs for timestamps" },

    { 0 }
    };

    Orphand_pst_dummy.lib_error = 0; /* just so we don't get warnings */

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

    if (lockfile) {
        Orphand_Lockfd = open(lockfile, O_RDWR|O_CREAT, 0644);
        if (Orphand_Lockfd == -1) {
            perror(lockfile);
            exit(EXIT_FAILURE);
        }
        if (flock(Orphand_Lockfd, LOCK_EX|LOCK_NB) == -1) {
            fprintf(stderr,
                    "Couldn't lock %s: %s\n",
                    lockfile, strerror(errno));

            fprintf(stderr,
                    "Ensure that no other instance of %s is running\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    start_orphand(path, Server.sweep_interval);
    return 0;
}
