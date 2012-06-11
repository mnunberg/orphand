#ifndef EMBHT_H_
#define EMBHT_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifndef EMBHT_INITIAL_FILL_SIZE
#define EMBHT_INITIAL_FILL_SIZE 1
#endif

#ifndef EMBHT_KEY_SIZE
#define EMBHT_KEY_SIZE 24
#endif

#ifndef EMBHT_VALUE_SIZE
#warning "No value size defined (using defaults)"
#define EMBHT_VALUE_SIZE 24
#endif


#define EMBHT_KLEN_MAX 65535
#define EMBHT_KLEN_INT (EMBHT_KLEN_MAX +1)
#define EMBHT_KLEN_AUTO (EMBHT_KLEN_MAX + 2)

/* If including the 'C' file directly, then this should be
 * defined to static.
 */

#ifndef EMBHT_API
#define EMBHT_API
#endif

typedef enum {
#define EMBHT_XFLAGS(X) \
    X(KPTR, use_key_pointers, 0x1)

#define X(c, fld, v) \
    EMBHT_F_##c = v,
    EMBHT_XFLAGS(X)
#undef X

    EMBHT_F_INVAL = 0x2
} embht_flags_t;

/**
 * The actual structure which stores the key
 * data and metadata
 */
typedef struct {
    uint32_t hash;
    unsigned int klen;
    union {
        char kds[EMBHT_KEY_SIZE];
        uint32_t kd32;
    } u_kdata;
} embht_key;

typedef struct {
    embht_key key;
#ifndef EMBHT_NO_VALUES
    union {
        char value[EMBHT_VALUE_SIZE];
        void *ptr;
    } u_value;
#endif

} embht_entry;

typedef struct {
    unsigned int fill;
    unsigned int capacity;
    embht_entry *array;
} embht_bucket;

typedef struct {
    embht_bucket *buckets;
    size_t nbuckets;

    /** size of each value (inclusive of the hashbucket size itself) */
    unsigned int elemsize;
    int use_key_pointers;

} embht_table;

typedef struct {
    embht_bucket *bh;
    embht_table *ht;
    long bidx;
    long aidx;

    unsigned long b_traversed;
    unsigned long b_remaining;
    int done;
} embht_iterator;

typedef struct {
    unsigned int full_buckets;
    unsigned int item_count;
} embht_statistics;

#define HBIDX(ht, a, ix) \
    ((embht_entry*)(a+( (ix) )))


#define HB_ptr(hb) ( (hb)->u_value.ptr )
#define HB_data(hb) ( (hb)->u_value.inl )

EMBHT_API
embht_table*
embht_make(size_t size, embht_flags_t flags);

EMBHT_API
embht_entry *
embht_fetch(const embht_table *ht,
            void *key,
            unsigned int nkey,
            int lval);

#define embht_fetchs(ht, lkey, lval) \
    embht_fetch(ht, lkey, sizeof(lkey)-1, lval)

#define embht_fetchz(ht, zkey, lval) \
    embht_fetch(ht, zkey, strlen(zkey), lval)

#define embht_fetchi(ht, ikey, lval) \
    embht_fetch(ht, (void*)(uintptr_t)(ikey), EMBHT_KLEN_INT, lval)

EMBHT_API
void *
embht_delete(embht_table *ht, void *key, unsigned int nkey);

#define embht_deletei(ht, ikey) \
    embht_delete(ht, (void*)(uintptr_t)ikey, EMBHT_KLEN_INT)

EMBHT_API
void
embht_destroy(embht_table *ht);

EMBHT_API
void
embht_stat(embht_table *ht, embht_statistics *stats);

EMBHT_API
void
embht_iterinit(embht_table *ht, embht_iterator *iter);

EMBHT_API
int
embht_iternext(embht_iterator *iter);

EMBHT_API
void
embht_iterdel(embht_iterator *iter);

/**
 * Gets the current bucket of the iterator
 */
#define embht_itercur(iter) (HBIDX( (iter)->ht, (iter)->bh->array, (iter)->aidx))
#define embht_iterkey(iter) embht_itercur(iter)->key
#define embht_iterval(iter) embht_itercur(iter)->u_value


#endif /* ORPHAND_PRIV_H_ */
