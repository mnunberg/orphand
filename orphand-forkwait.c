/**
 * This is intended to be used as an LD_PRELOAD injection as an alternative
 * to modifying an application to use the protocol directly.
 *
 * The main pid control system calls are replaced with wrappers. These wrappers
 * in turn call the SYS_ variants directly via syscall(2).
 */


#include "orphand.h"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>

#define PROGNAME "orphand-forkwait.so"

#ifndef LIBC_NAME
#define LIBC_NAME "libc.so.6"
#endif /* LIBC_NAME */

static pid_t (*real_wait)(int*);
static pid_t (*real_waitpid)(pid_t,int*,int);
static pid_t (*real_fork)(void);

/**
 * Because of possible thread safety issues, I can't think of any nice
 * way to cache any of this information..
 */

static void __attribute__((constructor))
init_real_functions(void)
{
    void *libc_handle = dlopen(LIBC_NAME, RTLD_LAZY);
    if (!libc_handle) {
        fprintf(stderr, "%s: Couldn't load '%s' (%s)\n",
                PROGNAME, LIBC_NAME, dlerror());
        abort();
    }
#define load_assert(base) \
    real_##base = dlsym(libc_handle, #base); \
    if (!real_##base) { \
        fprintf(stderr, "%s: Couldn't load '%s' (%s). Abort\n", \
                PROGNAME, #base, dlerror()); \
        abort(); \
    }
    load_assert(wait);
    load_assert(waitpid);
    load_assert(fork);

#undef load_assert
}

static void
send_orphand_message(pid_t parent,
                     pid_t child,
                     int action)
{
    orphand_message msg;
    struct msghdr mhdr;
    struct sockaddr_un saddr;
    struct iovec iov[3];
    char *sockpath;

    int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror(PROGNAME ": socket");
        return;
    }

    sockpath = getenv("ORPHAND_SOCKET");
    if (!sockpath) {
        sockpath = ORPHAND_DEFAULT_PATH;
    }

    saddr.sun_family = AF_UNIX;
    memcpy(saddr.sun_path, sockpath, strlen(sockpath)+1);

    msg.action = action;
    msg.child = child;
    msg.parent = parent;

    iov[0].iov_base = &msg.parent;
    iov[0].iov_len = sizeof(msg.parent);

    iov[1].iov_base = &msg.child;
    iov[1].iov_len = sizeof(msg.child);

    iov[2].iov_base = &msg.action;
    iov[2].iov_len = sizeof(msg.action);

    memset(&mhdr, 0, sizeof(mhdr));
    mhdr.msg_iov = iov;
    mhdr.msg_iovlen = 3;
    mhdr.msg_name = (struct sockaddr*)&saddr;
    mhdr.msg_namelen = sizeof(saddr);

    if (sendmsg(sock, &mhdr, MSG_WAITALL) == -1) {
        fprintf(stderr, "%s: sendmsg: %s\n", PROGNAME, strerror(errno));
    }

    close(sock);
}

pid_t fork(void)
{
    pid_t self, child;
    int errno_save;
    self = getpid();

    child = real_fork();
    if (child == 0) {
        return child;
    }

    errno_save = errno;

    if (getenv("ORPHAND_DEBUG")) {
        fprintf(stderr, "== %s == FORK %d => %d\n", PROGNAME, self, child);
    }

    send_orphand_message(self, child, ORPHAND_ACTION_REGISTER);
    errno = errno_save;
    return child;
}

static void
reap_common(pid_t child)
{
    pid_t self;
    int errno_save;
    self = getpid();

    errno_save = errno;

    if (getenv("ORPHAND_DEBUG")) {
        fprintf(stderr, "== %s == REAP %d => %d\n", PROGNAME, self, child);

    }
    send_orphand_message(self, child, ORPHAND_ACTION_UNREGISTER);

    errno = errno_save;
}

pid_t
wait(int *status)
{
    pid_t ret;
    ret = real_wait(status);
    if (ret == -1) {
        return ret;
    }

    reap_common(ret);
    return ret;
}

pid_t
waitpid(pid_t pid, int *status, int options)
{
    pid_t ret;
    ret = real_waitpid(pid, status, options);
    if (ret < 1) {
        return ret;
    }

    if (WIFEXITED(*status) == 0 && WIFSIGNALED(*status) == 0) {
        return ret;
    }
    reap_common(ret);
    return ret;
}
