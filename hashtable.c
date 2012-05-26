#include "orphand_priv.h"

hashtable*
ht_make(size_t size)
{
    hashtable *ret = malloc(sizeof(*ret));
    ret->heads = calloc(1, sizeof(struct bucket_head) * size);
    ret->nbuckets = size;
    ret->elemsize = sizeof(hashbucket);
    return ret;
}


hashbucket *
ht_fetch(const hashtable *ht, pid_t key, int lval)
{
    hashbucket *cur;
    pid_t hash = key % ht->nbuckets;
    size_t ii, ntraversed = 0;

    int first_empty = -1;

    struct bucket_head *bh = ht->heads + hash;
    DEBUG("Key %d maps to bucket %d", key, hash);

    /**
     * New bucket
     */
    if (!bh->fill) {
        if (lval) {
            DEBUG("Initializing empty bucket_head for %d", key);

            bh->array = calloc(INITIAL_FILL_SIZE, ht->elemsize);
            assert(bh->array);
            bh->capacity = 64;
            bh->fill = 1;

            cur = HBIDX(ht, bh->array, 0);

            goto GT_INIT_CUR;
        } else {
            return NULL;
        }
    }

    /**
     * Bucket exists, and has items
     */
    for (ii = 0; ii < bh->capacity && ntraversed < bh->fill; ii++) {

        cur = HBIDX(ht, bh->array, ii);

        if (cur->is_active) {
            if (cur->key == key) {
                DEBUG("Found key %d", key);
                return cur;
            }

            ntraversed++;

        } else if (lval && first_empty == -1 && cur->is_active == 0) {
            /**
             * If we are inserting an item, then mark the first empty slot
             * in the array, we might use this later
             */
            first_empty = ii;
        }
    }

    if (!lval) {
        return NULL;
    }

    if (first_empty >= 0) {
        /**
         * We have an empty slot, no need to reallocate
         */

        GT_FIRST_EMPTY:
        DEBUG("Have empty space for new item.. (%d)", key);
        cur = HBIDX(ht, bh->array, first_empty);
        bh->fill++;
        goto GT_INIT_CUR;
    } else {
        if (ntraversed < bh->capacity && bh->fill < bh->capacity) {
            /**
             * We are inserting here, so we are sure to find something that's
             * empty.
             */
            for (; ii < bh->capacity; ii++) {

                if (HBIDX(ht, bh->array, ii)->is_active == 0) {
                    first_empty = ii;
                    goto GT_FIRST_EMPTY;
                }
            }
            /* Not reached */
            fprintf(stderr, "We shouldn't be reaching here!\n");
            abort();

        } else {
            /**
             * We've reached our limit.
             */
            size_t offset, oldsize = bh->capacity;
            assert(bh->capacity == bh->fill);
            for (offset = 0; (1 << offset) < bh->capacity; offset++);

            bh->capacity = 1 << offset;
            bh->array = realloc(bh->array, bh->capacity);
            memset(bh->array + oldsize, 0, bh->capacity - oldsize);

            bh->fill++;
            cur = HBIDX(ht, bh->array, oldsize);
            goto GT_INIT_CUR;
        }
    }

    return NULL;

    GT_INIT_CUR:
    memset(cur, 0, ht->elemsize);
    cur->key = key;
    cur->is_active = 1;
    return cur;
}

void *
ht_delete(hashtable *ht, pid_t key)
{
    size_t ii;
    struct bucket_head *bh = ht->heads + (key % ht->nbuckets);
    if (!bh->fill) {
        return NULL;
    }
    for (ii = 0; ii < bh->fill; ii++) {
        hashbucket *cur = HBIDX(ht, bh->array, ii);
        if (cur->is_active && cur->key == key) {
            bh->fill--;
            cur->is_active = 0;
            return &cur->u_value;
        }
    }
    return NULL;
}

void
ht_destroy(hashtable *ht)
{
    size_t ii;
    for (ii = 0; ii < ht->nbuckets; ii++) {
        struct bucket_head *bh = ht->heads + ii;
        if (bh->array) {
            free(bh->array);
        }
    }
    free(ht->heads);
    free(ht);
}


void
ht_iterinit(hashtable *ht, ht_iterator *iter)
{
    iter->ht = ht;
    iter->aidx = -1;
    iter->bidx = 0;
    if (!ht->nbuckets) {
        iter->done = 1;
        WARN("Requested to initialize an empty table!");
    } else {
        iter->done = 0;
        iter->bh = ht->heads;
        iter->b_remaining = iter->bh->fill;
        iter->b_traversed = 0;
    }

}

int
ht_iternext(ht_iterator *iter)
{
    if (iter->done) {
        INFO("Iterator done");
        return 0;
    }

    while (1) {
        iter->aidx++;
        if (iter->aidx < iter->bh->capacity &&
                iter->b_traversed < iter->b_remaining) {
            /**
             * Advance the iterator until we have a valid position..
             */
            DEBUG("Trying to find suitable position..");

            while (HBIDX(iter->ht,
                         iter->bh->array,
                         iter->aidx)->is_active == 0) {

                iter->aidx++;
            }

            iter->b_traversed++;
            return 1;
        }


        iter->bidx++;
        iter->aidx = -1;
        iter->bh = iter->ht->heads + iter->bidx;

        if (iter->bidx >= iter->ht->nbuckets) {
            /* last bucket */
            DEBUG("No more bucket_heads in iterator");
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
    ERROR("We shouldn't have reached here");
    abort();
    return 0;
}

/**
 * Deletes the current hash element from the hash.
 */
void
ht_iterdel(ht_iterator *iter)
{
    HBIDX(iter->ht, iter->bh->array, iter->aidx)->is_active = 0;
    iter->bh->fill--;
}
