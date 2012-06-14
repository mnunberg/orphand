/**
 * This is intended to be used as an LD_PRELOAD injection as an alternative
 * to modifying an application to use the protocol directly.
 *
 * The main pid control system calls are replaced with wrappers. These wrappers
 * in turn call the SYS_ variants directly via syscall(2).
 */

#ifdef __linux__
#define _GNU_SOURCE
#endif /* __linux__ */

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
    void *libc_handle;

#ifdef __GLIBC__
#define _GNU_SOURCE
#include <dlfcn.h>
    libc_handle = RTLD_NEXT;
#else
#include <dlfcn.h>
    libc_handle = dlopen(LIBC_NAME, RTLD_LAZY);
    if (!libc_handle) {
        fprintf(stderr, "%s: Couldn't load '%s' (%s)\n",
                PROGNAME, LIBC_NAME, dlerror());
        abort();
    }
#endif /* __linux__ */

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
    ssize_t nw, nremaining, wtotal;
    char buf[12];
    uint32_t* bufp = (uint32_t*)buf;
    struct sockaddr_un saddr;
    char *sockpath;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
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

    bufp[0] = parent;
    bufp[1] = child;
    bufp[2] = action;

    if (connect(sock, (struct sockaddr*)&saddr, sizeof(saddr)) != 0) {
        fprintf(stderr, "%s: connect: %s\n", PROGNAME, strerror(errno));
        goto GT_END;
    }

    wtotal = 0;
    nremaining = 12;
    while (nremaining) {
        nw = send(sock, buf + wtotal, nremaining, 0);
        if (nw > 0) {
            nremaining -= nw;
            wtotal += nw;
        } else {
            if (nw == 0){
                fprintf(stderr, "%s: Remote closed connection\n", PROGNAME);
            } else {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "%s: send: %s\n", PROGNAME, strerror(errno));
            }
            break;
        }
    }

    GT_END:
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
