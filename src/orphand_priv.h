#ifndef ORPHAND_PRIV_H_
#define ORPHAND_PRIV_H_

#define ORPHAND_HAVE_PROCFS
#define ORPHAND_BUF_MAX (1<<17)
#define ORPHAND_BUF_SIZE 4096

#define EMBHT_API
#define EMBHT_KEY_SIZE sizeof(pid_t)
#define EMBHT_VALUE_SIZE sizeof(uint64_t)

#include "contrib/embhash.h"

#include "orphand.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

enum {
    LOGLVL_ERROR = 1,
    LOGLVL_WARN,
    LOGLVL_INFO,
    LOGLVL_DEBUG
};

enum {
    SOCKEV_RD = 0x1,
    SOCKEV_WR = 0x2,
    SOCKEV_ER = 0x4,
};

extern int Orphand_Loglevel;

#define _log_common(lvl, ...) \
    if (Orphand_Loglevel >= lvl) { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    }


#define DEBUG(...) _log_common(LOGLVL_DEBUG, __VA_ARGS__)
#define WARN(...) _log_common(LOGLVL_WARN, __VA_ARGS__)
#define ERROR(...) _log_common(LOGLVL_ERROR, __VA_ARGS__)
#define INFO(...) _log_common(LOGLVL_INFO, __VA_ARGS__);

struct orphand_buffer {
    size_t total;
    size_t used;
    size_t pos;
    char buf[ORPHAND_BUF_SIZE];
};

typedef struct orphand_client {
    int sockfd;
    struct orphand_buffer rcvbuf;
    struct orphand_buffer sndbuf;
} orphand_client;

typedef struct {
    int sock;
    int sweep_interval;
    int default_signum;
    void *ht;
    void *clients;

    /** Stuff for select() */
    fd_set fds_rd;
    fd_set fds_wr;

    int maxfd;
    int nsock;

    struct timeval tmo;
} orphand_server;


int
orphand_io_init(orphand_server *srv, const char *path);

void
orphand_process_message(orphand_server *srv,
                        orphand_client *cli,
                        const orphand_message *msg);

void
orphand_io_iteronce(orphand_server *srv);


/**
 * ffs how many times do i need to do this..
 * from glibc docs
 */



/* Subtract the `struct timeval' values X and Y,
   storing the result in RESULT.
   Return 1 if the difference is negative, otherwise 0.  */

static inline int
timeval_subtract (result, x, y)
     struct timeval *result, *x, *y;
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;

  /* Return 1 if result is negative. */
  return x->tv_sec < y->tv_sec;
}




#endif /* ORPHAND_PRIV_H_ */
