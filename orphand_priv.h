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

#define INITIAL_FILL_SIZE 64

#define _log_common(lvl, ...) \
    if (Orphand_Loglevel >= lvl) { \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    }


#define DEBUG(...) _log_common(LOGLVL_DEBUG, __VA_ARGS__)
#define WARN(...) _log_common(LOGLVL_WARN, __VA_ARGS__)
#define ERROR(...) _log_common(LOGLVL_ERROR, __VA_ARGS__)
#define INFO(...) _log_common(LOGLVL_INFO, __VA_ARGS__);

typedef struct hashbucket hashbucket;

struct hashbucket {
    pid_t key;
    int is_active;
    void *value;
};

struct bucket_head {
    size_t fill;
    size_t capacity;
    struct hashbucket *array;
};

typedef struct hashtable {
    struct bucket_head *heads;
    size_t nbuckets;
} hashtable;

typedef struct {
    struct bucket_head *bh;
    hashtable *ht;
    long bidx;
    long aidx;

    unsigned long b_traversed;
    unsigned long b_remaining;
    int done;
} ht_iterator;


hashtable*
ht_make(size_t size);

hashbucket *
ht_fetch(const hashtable *ht, pid_t key, int lval);

#define ht_store(ht, k, v) \
    (ht_fetch(ht, k, 1)->value = v)

void *
ht_delete(hashtable *ht, pid_t key);

void
ht_destroy(hashtable *ht);

void
ht_iterinit(hashtable *ht, ht_iterator *iter);


int
ht_iternext(ht_iterator *iter);

void
ht_iterdel(ht_iterator *iter);

#define ht_itercur(iter) ( (iter)->bh->array[ (iter)->aidx] )
#define ht_iterkey(iter) ht_itercur(iter).key
#define ht_iterval(iter) ht_itercur(iter).value

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
