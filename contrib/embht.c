#include "embhash.h"

static uint32_t
embht_strnhash(const char *s, unsigned int len)
{
    uint32_t hash;
    unsigned int i;
    for(hash = i = 0; i < len; ++i)
    {
        hash += s[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;

    /**
    uint32_t hash = 5381;
    int ii;
    for (ii = 0; ii < len; ii++) {
        hash = ((hash<<5) + hash) + s[ii];
    }
    return hash;
    */
}

static uint32_t
embht_hash_key(const void *k, unsigned int klen) {
    if (klen == EMBHT_KLEN_INT) {
        return (uint32_t)(uintptr_t)k;
    } else {
        return embht_strnhash((const char*)k, klen);
    }
    return 0;
}

static int
embht_cmp_key(const embht_table *ht,
              const void *ukey,
              unsigned int uklen,
              embht_key *ekey) {

    if (uklen != ekey->klen) {
        return 1;
    }

    if (uklen == EMBHT_KLEN_INT) {
        return !((uint32_t)(uintptr_t)ukey == ekey->u_kdata.kd32);
    }
    return memcmp(ukey, ekey->u_kdata.kds, uklen);
}

EMBHT_API
embht_table*
embht_make(size_t size, embht_flags_t flags)
{
    embht_table *ret = malloc(sizeof(*ret));
    ret->buckets = calloc(1, sizeof(embht_bucket) * size);
    ret->nbuckets = size;
#define X(c, fld, v) \
    if (flags & EMBHT_F_##c) { ret->fld = 1; }
    EMBHT_XFLAGS(X)
#undef X

    return ret;
}

struct embht_search_ctx {
    int first_empty;
    unsigned int pos;
    embht_bucket *bh;
    uint32_t hash;
};

static
embht_entry *
embht_fetchonly(const embht_table *ht,
                void *key,
                unsigned int nkey,
                struct embht_search_ctx *search)
{
    embht_entry *cur;
    unsigned int ii, nchecked = 0, check_max;

    uint32_t hash = embht_hash_key(key, nkey);
    embht_bucket *bh = ht->buckets + (hash % ht->nbuckets );

    if (search) {
        search->bh = bh;
        search->hash = hash;
    }

    check_max = bh->fill;

    for (ii = 0, nchecked = 0, cur = bh->array;
            ii < bh->capacity && nchecked < check_max;
            ii++, cur++) {

        if (cur->key.klen == 0) {
            if (search && search->first_empty < 0) {
                search->first_empty = ii;
            }
            continue;
        }

        nchecked++;


        if (cur->key.hash == hash &&
                embht_cmp_key(ht, key, nkey, &cur->key) == 0) {
            goto GT_RET;
        }
    }

    cur = NULL;

    GT_RET:
    if (search) {
        search->pos = ii;
    }
    return cur;
}

static
embht_entry *
embht_fetchstore(const embht_table *ht,
                 void *key,
                 unsigned int nkey)
{
    embht_entry *cur;
    embht_bucket *bh;

    struct embht_search_ctx ctx = { 0 };
    ctx.first_empty = -1;

    if ( (cur = embht_fetchonly(ht, key, nkey, &ctx)) ) {
        return cur;
    }

    bh = ctx.bh;

    if (!bh->fill) {
        if (!bh->capacity) {
            bh->array = calloc(EMBHT_INITIAL_FILL_SIZE,
                               sizeof(*(bh->array)));
            bh->capacity = EMBHT_INITIAL_FILL_SIZE;
        }
        cur = bh->array;
        goto GT_INIT_CUR;
    }

    if (ctx.first_empty >= 0) {
        cur = bh->array + ctx.first_empty;
        goto GT_INIT_CUR;
    }

    for (; ctx.pos < bh->capacity; ctx.pos++) {

        if (bh->array[ctx.pos].key.klen == 0) {
            cur = bh->array + ctx.pos;
            goto GT_INIT_CUR;
        }
    }


    {
        size_t offset, oldsize = bh->capacity;
        assert (bh->capacity == bh->fill);
        for (offset = 0; (1<<offset) <= bh->capacity; offset++);

        bh->capacity = 1<<offset;

#define _REINIT(fld) \
    bh->fld = realloc(bh->fld, bh->capacity * (sizeof(*(bh->fld)))); \
    memset(bh->fld + oldsize, 0, (bh->capacity - oldsize) * sizeof(*(bh->fld)));

        _REINIT(array);
#undef _REINIT

        cur = bh->array + oldsize;
    }

    GT_INIT_CUR:
    bh->fill++;
    assert(bh->fill <= bh->capacity);
    cur->key.klen = nkey;
    cur->key.hash = ctx.hash;
    if (nkey == EMBHT_KLEN_INT) {
        cur->key.u_kdata.kd32 = (uint32_t)(uintptr_t)key;
    } else {
        memcpy(cur->key.u_kdata.kds, key, nkey);
    }

    return cur;
}

EMBHT_API
embht_entry *
embht_fetch(const embht_table *ht,
            void *key,
            unsigned int nkey,
            int lval)
{
    if (!lval) {
        return embht_fetchonly(ht, key, nkey, NULL);
    } else {
        return embht_fetchstore(ht, key, nkey);
    }
}

EMBHT_API
void *
embht_delete(embht_table *ht, void *key, unsigned int nkey)
{
    struct embht_search_ctx ctx = { 0 };
    embht_entry *ent = embht_fetchonly(ht, key, nkey, &ctx);

    if (!ent) {
        return NULL;
    }

    ctx.bh->fill--;
    ent->key.klen = 0;
    return &ent->u_value;
}

EMBHT_API
void
embht_destroy(embht_table *ht)
{
    size_t ii;
    for (ii = 0; ii < ht->nbuckets; ii++) {
        embht_bucket *bh = ht->buckets + ii;
        if (bh->array) {
            free(bh->array);
        }
    }
    free(ht->buckets);
    free(ht);
}

EMBHT_API
void
embht_iterinit(embht_table *ht, embht_iterator *iter)
{
    iter->ht = ht;
    iter->aidx = -1;
    iter->bidx = 0;
    if (!ht->nbuckets) {
        iter->done = 1;
    } else {
        iter->done = 0;
        iter->bh = ht->buckets;
        iter->b_remaining = iter->bh->fill;
        iter->b_traversed = 0;
    }

}

EMBHT_API
int
embht_iternext(embht_iterator *iter)
{
    if (iter->done) {
        return 0;
    }

    while (1) {
        iter->aidx++;
        if (iter->aidx < iter->bh->capacity &&
                iter->b_traversed < iter->b_remaining) {
            /**
             * Advance the iterator until we have a valid position..
             */

            while (HBIDX(iter->ht,
                         iter->bh->array,
                         iter->aidx)->key.klen == 0) {

                iter->aidx++;
            }

            iter->b_traversed++;
            return 1;
        }


        iter->bidx++;
        iter->aidx = -1;
        iter->bh = iter->ht->buckets + iter->bidx;

        if (iter->bidx >= iter->ht->nbuckets) {
            /* last bucket */
            iter->done = 1;
            return 0;
        }

        if ( (iter->b_remaining = iter->bh->fill) == 0) {
            /* empty bucket */
            iter->b_traversed = 1;
            continue;
        }

        /* not the last, not empty */

        iter->b_traversed = 0;
    }
    /* not reached */
    abort();
    return 0;
}

/**
 * Deletes the current hash element from the hash.
 */
EMBHT_API
void
embht_iterdel(embht_iterator *iter)
{
    embht_entry *ent = HBIDX(iter->ht, iter->bh->array, iter->aidx);

    ent->key.klen = 0;
    memset(&ent->u_value, 0, sizeof(ent->u_value));

    iter->bh->fill--;
}

EMBHT_API
void
embht_stat(embht_table *ht, embht_statistics *stats)
{
    int ii;
    stats->full_buckets = 0;
    stats->item_count = 0;

    for (ii = 0; ii < ht->nbuckets; ii++) {
        embht_bucket *hb = ht->buckets + ii;
        if (hb->fill) {
            stats->item_count += hb->fill;
            stats->full_buckets++;
        }
    }
}
