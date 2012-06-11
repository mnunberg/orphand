#ifndef ORPHAND_PRIV_H_
#define ORPHAND_PRIV_H_

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
