// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef BTREE_STATIC
#include "btree.h"
#else
#define BTREE_EXTERN static
#endif

#ifndef BTREE_EXTERN
#define BTREE_EXTERN
#endif

static void *(*_btree_malloc)(size_t) = NULL;
static void (*_btree_free)(void *) = NULL;

// DEPRECATED: use `btree_new_with_allocator`
BTREE_EXTERN
void btree_set_allocator(void *(malloc)(size_t), void (*free)(void*)) {
    _btree_malloc = malloc;
    _btree_free = free;
}

enum btree_delact {
    BTREE_DELKEY, BTREE_POPFRONT, BTREE_POPBACK, BTREE_POPMAX,
};

static size_t btree_align_size(size_t size) {
    size_t boundary = sizeof(uintptr_t);
    return size < boundary ? boundary :
           size&(boundary-1) ? size+boundary-(size&(boundary-1)) :
           size;
}

#ifdef BTREE_NOATOMICS
typedef int btree_rc_t;
static int btree_rc_load(btree_rc_t *ptr) {
    return *ptr;
}
static int btree_rc_fetch_sub(btree_rc_t *ptr, int val) {
    int rc = *ptr;
    *ptr -= val;
    return rc;
}
static int btree_rc_fetch_add(btree_rc_t *ptr, int val) {
    int rc = *ptr;
    *ptr += val;
    return rc;
}
#else
#include <stdatomic.h>
typedef atomic_int btree_rc_t;
static int btree_rc_load(btree_rc_t *ptr) {
    return atomic_load(ptr);
}
static int btree_rc_fetch_sub(btree_rc_t *ptr, int delta) {
    return atomic_fetch_sub(ptr, delta);
}
static int btree_rc_fetch_add(btree_rc_t *ptr, int delta) {
    return atomic_fetch_add(ptr, delta);
}
#endif

struct btree_node {
    btree_rc_t rc;
    bool leaf;
    size_t nitems:16;
    char *items;
    struct btree_node *children[];
};

struct btree {
    void *(*malloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
    int (*compare)(const void *a, const void *b, void *udata);
    int (*searcher)(const void *items, size_t nitems, const void *key,
        bool *found, void *udata);
    bool (*item_clone)(const void *item, void *into, void *udata);
    void (*item_free)(const void *item, void *udata);
    void *udata;             // user data
    struct btree_node *root; // root node or NULL if empty tree
    size_t count;            // number of items in tree
    size_t height;           // height of tree from root to leaf
    size_t max_items;        // max items allowed per node before needing split
    size_t min_items;        // min items allowed per node before needing join
    size_t elsize;           // size of user item
    bool oom;                // last write operation failed due to no memory
    size_t spare_elsize;     // size of each spare element. This is aligned
    char spare_data[];       // spare element spaces for various operations
};

static void *btree_spare_at(const struct btree *btree, size_t index) {
    return (void*)(btree->spare_data+btree->spare_elsize*index);
}

BTREE_EXTERN
void btree_set_searcher(struct btree *btree,
    int (*searcher)(const void *items, size_t nitems, const void *key,
        bool *found, void *udata))
{
    btree->searcher = searcher;
}

#define BTREE_NSPARES 4
#define BTREE_SPARE_RETURN btree_spare_at(btree, 0) // returned values
#define BTREE_SPARE_NODE   btree_spare_at(btree, 1) // clone in btree_node_copy
#define BTREE_SPARE_POPMAX btree_spare_at(btree, 2) // btree_delete popmax
#define BTREE_SPARE_CLONE  btree_spare_at(btree, 3) // cloned inputs

static void *btree_get_item_at(struct btree *btree, struct btree_node *node,
    size_t index)
{
    return node->items+btree->elsize*index;
}

static void btree_set_item_at(struct btree *btree, struct btree_node *node,
    size_t index, const void *item)
{
    void *slot = btree_get_item_at(btree, node, index);
    memcpy(slot, item, btree->elsize);
}

static void btree_swap_item_at(struct btree *btree, struct btree_node *node,
    size_t index, const void *item, void *into)
{
    void *ptr = btree_get_item_at(btree, node, index);
    memcpy(into, ptr, btree->elsize);
    memcpy(ptr, item, btree->elsize);
}

static void btree_copy_item_into(struct btree *btree,
    struct btree_node *node, size_t index, void *into)
{
    memcpy(into, btree_get_item_at(btree, node, index), btree->elsize);
}

static void btree_node_shift_right(struct btree *btree, struct btree_node *node,
    size_t index)
{
    size_t num_items_to_shift = node->nitems - index;
    memmove(node->items+btree->elsize*(index+1),
        node->items+btree->elsize*index, num_items_to_shift*btree->elsize);
    if (!node->leaf) {
        memmove(&node->children[index+1], &node->children[index],
            (num_items_to_shift+1)*sizeof(struct btree_node*));
    }
    node->nitems++;
}

static void btree_node_shift_left(struct btree *btree, struct btree_node *node,
    size_t index, bool for_merge)
{
    size_t num_items_to_shift = node->nitems - index - 1;
    memmove(node->items+btree->elsize*index,
        node->items+btree->elsize*(index+1), num_items_to_shift*btree->elsize);
    if (!node->leaf) {
        if (for_merge) {
            index++;
            num_items_to_shift--;
        }
        memmove(&node->children[index], &node->children[index+1],
            (num_items_to_shift+1)*sizeof(struct btree_node*));
    }
    node->nitems--;
}

static void btree_copy_item(struct btree *btree, struct btree_node *node_a,
    size_t index_a, struct btree_node *node_b, size_t index_b)
{
    memcpy(btree_get_item_at(btree, node_a, index_a),
        btree_get_item_at(btree, node_b, index_b), btree->elsize);
}

static void btree_node_join(struct btree *btree, struct btree_node *left,
    struct btree_node *right)
{
    memcpy(left->items+btree->elsize*left->nitems, right->items,
        right->nitems*btree->elsize);
    if (!left->leaf) {
        memcpy(&left->children[left->nitems], &right->children[0],
            (right->nitems+1)*sizeof(struct btree_node*));
    }
    left->nitems += right->nitems;
}

static int _btree_compare(const struct btree *btree, const void *a,
    const void *b)
{
    return btree->compare(a, b, btree->udata);
}

static size_t btree_node_bsearch(const struct btree *btree,
    struct btree_node *node, const void *key, bool *found)
{
    size_t i = 0;
    size_t n = node->nitems;
    while ( i < n ) {
        size_t j = (i + n) >> 1;
        void *item = btree_get_item_at((void*)btree, node, j);
        int cmp = _btree_compare(btree, key, item);
        if (cmp == 0) {
            *found = true;
            return j;
        } else if (cmp < 0) {
            n = j;
        } else {
            i = j+1;
        }
    }
    *found = false;
    return i;
}

static int btree_node_bsearch_hint(const struct btree *btree,
    struct btree_node *node, const void *key, bool *found, uint64_t *hint,
    int depth)
{
    int low = 0;
    int high = node->nitems-1;
    if (hint && depth < 8) {
        size_t index = (size_t)((uint8_t*)hint)[depth];
        if (index > 0) {
            if (index > (size_t)(node->nitems-1)) {
                index = node->nitems-1;
            }
            void *item = btree_get_item_at((void*)btree, node, (size_t)index);
            int cmp = _btree_compare(btree, key, item);
            if (cmp == 0) {
                *found = true;
                return index;
            }
            if (cmp > 0) {
                low = index+1;
            } else {
                high = index-1;
            }
        }
    }
    int index;
    while ( low <= high ) {
        int mid = (low + high) / 2;
        void *item = btree_get_item_at((void*)btree, node, (size_t)mid);
        int cmp = _btree_compare(btree, key, item);
        if (cmp == 0) {
            *found = true;
            index = mid;
            goto done;
        }
        if (cmp < 0) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    *found = false;
    index = low;
done:
    if (hint && depth < 8) {
        ((uint8_t*)hint)[depth] = (uint8_t)index;
    }
    return index;
}

static size_t btree_memsize(size_t elsize, size_t *spare_elsize) {
    size_t size = btree_align_size(sizeof(struct btree));
    size_t elsize_aligned = btree_align_size(elsize);
    size += elsize_aligned * BTREE_NSPARES;
    if (spare_elsize) *spare_elsize = elsize_aligned;
    return size;
}

BTREE_EXTERN
struct btree *btree_new_with_allocator(
    void *(*malloc_)(size_t),
    void *(*realloc_)(void *, size_t),
    void (*free_)(void*),
    size_t elsize,
    size_t max_items,
    int (*compare)(const void *a, const void *b, void *udata),
    void *udata)
{
    (void)realloc_; // realloc not used
    malloc_ = malloc_ ? malloc_ : _btree_malloc ? _btree_malloc : malloc;
    free_ = free_ ? free_ : _btree_free ? _btree_free : free;

    // normalize max_items
    size_t spare_elsize;
    size_t size = btree_memsize(elsize, &spare_elsize);
    struct btree *btree = malloc_(size);
    if (!btree) {
        return NULL;
    }
    memset(btree, 0, size);
    size_t deg = max_items/2;
    deg = deg == 0 ? 128 : deg == 1 ? 2 : deg;
    btree->max_items = deg*2 - 1; // max items per node. max children is +1
    if (btree->max_items > 2045) {
        // there must be a reasonable limit.
        btree->max_items = 2045;
    }
    btree->min_items = btree->max_items / 2;
    btree->compare = compare;
    btree->elsize = elsize;
    btree->udata = udata;
    btree->malloc = malloc_;
    btree->free = free_;
    btree->spare_elsize = spare_elsize;
    return btree;
}

BTREE_EXTERN
struct btree *btree_new(size_t elsize, size_t max_items,
    int (*compare)(const void *a, const void *b, void *udata), void *udata)
{
    return btree_new_with_allocator(NULL, NULL, NULL, elsize, max_items,
        compare, udata);
}

static size_t btree_node_size(struct btree *btree, bool leaf,
    size_t *items_offset)
{
    size_t size = sizeof(struct btree_node);
    if (!leaf) {
        // add children as flexible array
        size += sizeof(struct btree_node*)*(btree->max_items+1);
    }
    if (items_offset) *items_offset = size;
    size += btree->elsize*btree->max_items;
    size = btree_align_size(size);
    return size;
}

static struct btree_node *btree_node_new(struct btree *btree, bool leaf) {
    size_t items_offset;
    size_t size = btree_node_size(btree, leaf, &items_offset);
    struct btree_node *node = btree->malloc(size);
    if (!node) {
        return NULL;
    }
    memset(node, 0, size);
    node->leaf = leaf;
    node->items = (char*)node+items_offset;
    return node;
}

static void btree_node_free(struct btree *btree, struct btree_node *node) {
    if (btree_rc_fetch_sub(&node->rc, 1) > 0) {
        return;
    }
    if (!node->leaf) {
        for (size_t i = 0; i < (size_t)(node->nitems+1); i++) {
            btree_node_free(btree, node->children[i]);
        }
    }
    if (btree->item_free) {
        for (size_t i = 0; i < node->nitems; i++) {
            void *item = btree_get_item_at(btree, node, i);
            btree->item_free(item, btree->udata);
        }
    }
    btree->free(node);
}

static struct btree_node *btree_node_copy(struct btree *btree,
    struct btree_node *node)
{
    struct btree_node *node2 = btree_node_new(btree, node->leaf);
    if (!node2) {
        return NULL;
    }
    node2->nitems = node->nitems;
    size_t items_cloned = 0;
    if (!node2->leaf) {
        for (size_t i = 0; i < (size_t)(node2->nitems+1); i++) {
            node2->children[i] = node->children[i];
            btree_rc_fetch_add(&node2->children[i]->rc, 1);
        }
    }
    if (btree->item_clone) {
        for (size_t i = 0; i < node2->nitems; i++) {
            void *item = btree_get_item_at(btree, node, i);
            if (!btree->item_clone(item, BTREE_SPARE_NODE, btree->udata)) {
                goto failed;
            }
            btree_set_item_at(btree, node2, i, BTREE_SPARE_NODE);
            items_cloned++;
        }
    } else {
        for (size_t i = 0; i < node2->nitems; i++) {
            void *item = btree_get_item_at(btree, node, i);
            btree_set_item_at(btree, node2, i, item);
        }
    }
    return node2;
failed:
    if (!node2->leaf) {
        for (size_t i = 0; i < (size_t)(node2->nitems+1); i++) {
            btree_rc_fetch_sub(&node2->children[i]->rc, 1);
        }
    }
    if (btree->item_free) {
        for (size_t i = 0; i < items_cloned; i++) {
            void *item = btree_get_item_at(btree, node2, i);
            btree->item_free(item, btree->udata);
        }
    }
    btree->free(node2);
    return NULL;
}

#define btree_cow_node_or(bnode, code) { \
    if (btree_rc_load(&(bnode)->rc) > 0) { \
        struct btree_node *node2 = btree_node_copy(btree, (bnode)); \
        if (!node2) { code; } \
        btree_node_free(btree, bnode); \
        (bnode) = node2; \
    } \
}

BTREE_EXTERN
void btree_clear(struct btree *btree) {
    if (btree->root) {
        btree_node_free(btree, btree->root);
    }
    btree->oom = false;
    btree->root = NULL;
    btree->count = 0;
    btree->height = 0;
}

BTREE_EXTERN
void btree_free(struct btree *btree) {
    btree_clear(btree);
    btree->free(btree);
}

BTREE_EXTERN
void btree_set_item_callbacks(struct btree *btree,
    bool (*clone)(const void *item, void *into, void *udata),
    void (*free)(const void *item, void *udata))
{
    btree->item_clone = clone;
    btree->item_free = free;
}

BTREE_EXTERN
struct btree *btree_clone(struct btree *btree) {
    if (!btree) {
        return NULL;
    }
    size_t size = btree_memsize(btree->elsize, NULL);
    struct btree *btree2 = btree->malloc(size);
    if (!btree2) {
        return NULL;
    }
    memcpy(btree2, btree, size);
    if (btree2->root) btree_rc_fetch_add(&btree2->root->rc, 1);
    return btree2;
}

static size_t btree_search(const struct btree *btree, struct btree_node *node,
    const void *key, bool *found, uint64_t *hint, int depth)
{
    if (!hint && !btree->searcher) {
        return btree_node_bsearch(btree, node, key, found);
    }
    if (btree->searcher) {
        return btree->searcher(node->items, node->nitems, key, found,
            btree->udata);
    }
    return btree_node_bsearch_hint(btree, node, key, found, hint, depth);
}

enum btree_mut_result {
    BTREE_NOCHANGE,
    BTREE_NOMEM,
    BTREE_MUST_SPLIT,
    BTREE_INSERTED,
    BTREE_REPLACED,
    BTREE_DELETED,
};

static void btree_node_split(struct btree *btree, struct btree_node *node,
    struct btree_node **right, void **median)
{
    *right = btree_node_new(btree, node->leaf);
    if (!*right) {
        return; // NOMEM
    }
    size_t mid = btree->max_items / 2;
    *median = btree_get_item_at(btree, node, mid);
    (*right)->leaf = node->leaf;
    (*right)->nitems = node->nitems-(mid+1);
    memmove((*right)->items, node->items+btree->elsize*(mid+1),
        (*right)->nitems*btree->elsize);
    if (!node->leaf) {
        for (size_t i = 0; i <= (*right)->nitems; i++) {
            (*right)->children[i] = node->children[mid+1+i];
        }
    }
    node->nitems = mid;
}

static enum btree_mut_result btree_node_set(struct btree *btree,
    struct btree_node *node, const void *item, uint64_t *hint, int depth)
{
    bool found = false;
    size_t i = btree_search(btree, node, item, &found, hint, depth);
    if (found) {
        btree_swap_item_at(btree, node, i, item, BTREE_SPARE_RETURN);
        return BTREE_REPLACED;
    }
    if (node->leaf) {
        if (node->nitems == btree->max_items) {
            return BTREE_MUST_SPLIT;
        }
        btree_node_shift_right(btree, node, i);
        btree_set_item_at(btree, node, i, item);
        return BTREE_INSERTED;
    }
    btree_cow_node_or(node->children[i], return BTREE_NOMEM);
    enum btree_mut_result result = btree_node_set(btree, node->children[i],
        item, hint, depth+1);
    if (result == BTREE_INSERTED || result == BTREE_REPLACED) {
        return result;
    } else if (result == BTREE_NOMEM) {
        return BTREE_NOMEM;
    }
    // Split the child node
    if (node->nitems == btree->max_items) {
        return BTREE_MUST_SPLIT;
    }
    void *median = NULL;
    struct btree_node *right = NULL;
    btree_node_split(btree, node->children[i], &right, &median);
    if (!right) {
        return BTREE_NOMEM;
    }
    btree_node_shift_right(btree, node, i);
    btree_set_item_at(btree, node, i, median);
    node->children[i+1] = right;
    return btree_node_set(btree, node, item, hint, depth);
}

static void *btree_set0(struct btree *btree, const void *item, uint64_t *hint,
    bool no_item_clone)
{
    btree->oom = false;
    bool item_cloned = false;
    if (btree->item_clone && !no_item_clone) {
        if (!btree->item_clone(item, BTREE_SPARE_CLONE, btree->udata)) {
            goto oom;
        }
        item = BTREE_SPARE_CLONE;
        item_cloned = true;
    }
    if (!btree->root) {
        btree->root = btree_node_new(btree, true);
        if (!btree->root) {
            goto oom;
        }
        btree_set_item_at(btree, btree->root, 0, item);
        btree->root->nitems = 1;
        btree->count++;
        btree->height++;
        return NULL;
    }
    btree_cow_node_or(btree->root, goto oom);
    enum btree_mut_result result;
set:
    result = btree_node_set(btree, btree->root, item, hint, 0);
    if (result == BTREE_REPLACED) {
        if (btree->item_free) {
            btree->item_free(BTREE_SPARE_RETURN, btree->udata);
        }
        return BTREE_SPARE_RETURN;
    } else if (result == BTREE_INSERTED) {
        btree->count++;
        return NULL;
    } else if (result == BTREE_NOMEM) {
        goto oom;
    }
    void *old_root = btree->root;
    struct btree_node *new_root = btree_node_new(btree, false);
    if (!new_root) {
        goto oom;
    }
    struct btree_node *right = NULL;
    void *median = NULL;
    btree_node_split(btree, old_root, &right, &median);
    if (!right) {
        btree->free(new_root);
        goto oom;
    }
    btree->root = new_root;
    btree->root->children[0] = old_root;
    btree_set_item_at(btree, btree->root, 0, median);
    btree->root->children[1] = right;
    btree->root->nitems = 1;
    btree->height++;
    goto set;
oom:
    if (btree->item_free) {
        if (item_cloned) {
            btree->item_free(BTREE_SPARE_CLONE, btree->udata);
        }
    }
    btree->oom = true;
    return NULL;
}

static const void *btree_get0(const struct btree *btree, const void *key,
    uint64_t *hint)
{
    struct btree_node *node = btree->root;
    if (!node) {
        return NULL;
    }
    bool found;
    int depth = 0;
    while (1) {
        size_t i = btree_search(btree, node, key, &found, hint, depth);
        if (found) {
            return btree_get_item_at((void*)btree, node, i);
        }
        if (node->leaf) {
            return NULL;
        }
        node = node->children[i];
        depth++;
    }
}

static void btree_node_rebalance(struct btree *btree, struct btree_node *node,
    size_t i)
{
    if (i == node->nitems) {
        i--;
    }

    struct btree_node *left  = node->children[i];
    struct btree_node *right = node->children[i+1];

    // assert(btree_rc_load(&left->rc)==0);
    // assert(btree_rc_load(&right->rc)==0);

    if (left->nitems + right->nitems < btree->max_items) {
        // Merges the left and right children nodes together as a single node
        // that includes (left,item,right), and places the contents into the
        // existing left node. Delete the right node altogether and move the
        // following items and child nodes to the left by one slot.

        // merge (left,item,right)
        btree_copy_item(btree, left, left->nitems, node, i);
        left->nitems++;
        btree_node_join(btree, left, right);
        btree->free(right);
        btree_node_shift_left(btree, node, i, true);
    } else if (left->nitems > right->nitems) {
        // move left -> right over one slot

        // Move the item of the parent node at index into the right-node first
        // slot, and move the left-node last item into the previously moved
        // parent item slot.
        btree_node_shift_right(btree, right, 0);
        btree_copy_item(btree, right, 0, node, i);
        if (!left->leaf) {
            right->children[0] = left->children[left->nitems];
        }
        btree_copy_item(btree, node, i, left, left->nitems-1);
        if (!left->leaf) {
            left->children[left->nitems] = NULL;
        }
        left->nitems--;
    } else {
        // move right -> left

        // Same as above but the other direction
        btree_copy_item(btree, left, left->nitems, node, i);
        if (!left->leaf) {
            left->children[left->nitems+1] = right->children[0];
        }
        left->nitems++;
        btree_copy_item(btree, node, i, right, 0);
        btree_node_shift_left(btree, right, 0, false);
    }
}

static enum btree_mut_result btree_node_delete(struct btree *btree,
    struct btree_node *node, enum btree_delact act, size_t index,
    const void *key, void *prev, uint64_t *hint, int depth)
{
    size_t i = 0;
    bool found = false;
    if (act == BTREE_DELKEY) {
        i = btree_search(btree, node, key, &found, hint, depth);
    } else if (act == BTREE_POPMAX) {
        i = node->nitems-1;
        found = true;
    } else if (act == BTREE_POPFRONT) {
        i = 0;
        found = node->leaf;
    } else if (act == BTREE_POPBACK) {
        if (!node->leaf) {
            i = node->nitems;
            found = false;
        } else {
            i = node->nitems-1;
            found = true;
        }
    }
    if (node->leaf) {
        if (found) {
            // Item was found in leaf, copy its contents and delete it.
            // This might cause the number of items to drop below min_items,
            // and it so, the caller will take care of the rebalancing.
            btree_copy_item_into(btree, node, i, prev);
            btree_node_shift_left(btree, node, i, false);
            return BTREE_DELETED;
        }
        return BTREE_NOCHANGE;
    }
    enum btree_mut_result result;
    if (found) {
        if (act == BTREE_POPMAX) {
            // Popping off the max item into into its parent branch to maintain
            // a balanced tree.
            i++;
            btree_cow_node_or(node->children[i], return BTREE_NOMEM);
            btree_cow_node_or(node->children[i==node->nitems?i-1:i+1],
                return BTREE_NOMEM);
            result = btree_node_delete(btree, node->children[i], BTREE_POPMAX,
                0, NULL, prev, hint, depth+1);
            if (result == BTREE_NOMEM) {
                return BTREE_NOMEM;
            }
            result = BTREE_DELETED;
        } else {
            // item was found in branch, copy its contents, delete it, and
            // begin popping off the max items in child nodes.
            btree_copy_item_into(btree, node, i, prev);
            btree_cow_node_or(node->children[i], return BTREE_NOMEM);
            btree_cow_node_or(node->children[i==node->nitems?i-1:i+1],
                return BTREE_NOMEM);
            result = btree_node_delete(btree, node->children[i], BTREE_POPMAX,
                0, NULL, BTREE_SPARE_POPMAX, hint, depth+1);
            if (result == BTREE_NOMEM) {
                return BTREE_NOMEM;
            }
            btree_set_item_at(btree, node, i, BTREE_SPARE_POPMAX);
            result = BTREE_DELETED;
        }
    } else {
        // item was not found in this branch, keep searching.
        btree_cow_node_or(node->children[i], return BTREE_NOMEM);
        btree_cow_node_or(node->children[i==node->nitems?i-1:i+1],
            return BTREE_NOMEM);
        result = btree_node_delete(btree, node->children[i], act, index, key,
            prev, hint, depth+1);
    }
    if (result != BTREE_DELETED) {
        return result;
    }
    if (node->children[i]->nitems < btree->min_items) {
        btree_node_rebalance(btree, node, i);
    }
    return BTREE_DELETED;
}

static void *btree_delete0(struct btree *btree, enum btree_delact act,
    size_t index, const void *key, uint64_t *hint)
{
    btree->oom = false;
    if (!btree->root) {
        return NULL;
    }
    btree_cow_node_or(btree->root, goto oom);
    enum btree_mut_result result = btree_node_delete(btree, btree->root, act,
        index, key, BTREE_SPARE_RETURN, hint, 0);
    if (result == BTREE_NOCHANGE) {
        return NULL;
    } else if (result == BTREE_NOMEM) {
        goto oom;
    }
    if (btree->root->nitems == 0) {
        struct btree_node *old_root = btree->root;
        if (!btree->root->leaf) {
            btree->root = btree->root->children[0];
        } else {
            btree->root = NULL;
        }
        btree->free(old_root);
        btree->height--;
    }
    btree->count--;
    if (btree->item_free) {
        btree->item_free(BTREE_SPARE_RETURN, btree->udata);
    }
    return BTREE_SPARE_RETURN;
oom:
    btree->oom = true;
    return NULL;
}

BTREE_EXTERN
const void *btree_set_hint(struct btree *btree, const void *item,
    uint64_t *hint)
{
    return btree_set0(btree, item, hint, false);
}

BTREE_EXTERN
const void *btree_set(struct btree *btree, const void *item) {
    return btree_set0(btree, item, NULL, false);
}

BTREE_EXTERN
const void *btree_get_hint(const struct btree *btree, const void *key,
    uint64_t *hint)
{
    return btree_get0(btree, key, hint);
}

BTREE_EXTERN
const void *btree_get(const struct btree *btree, const void *key) {
    return btree_get0(btree, key, NULL);
}

BTREE_EXTERN
const void *btree_delete_hint(struct btree *btree, const void *key,
    uint64_t *hint)
{
    return btree_delete0(btree, BTREE_DELKEY, 0, key, hint);
}

BTREE_EXTERN
const void *btree_delete(struct btree *btree, const void *key) {
    return btree_delete0(btree, BTREE_DELKEY, 0, key, NULL);
}

BTREE_EXTERN
const void *btree_pop_min(struct btree *btree) {
    btree->oom = false;
    if (btree->root) {
        btree_cow_node_or(btree->root, goto oom);
        struct btree_node *node = btree->root;
        while (1) {
            if (node->leaf) {
                if (node->nitems > btree->min_items) {
                    size_t i = 0;
                    btree_copy_item_into(btree, node, i, BTREE_SPARE_RETURN);
                    btree_node_shift_left(btree, node, i, false);
                    if (btree->item_free) {
                        btree->item_free(BTREE_SPARE_RETURN, btree->udata);
                    }
                    btree->count--;
                    return BTREE_SPARE_RETURN;
                }
                break;
            }
            btree_cow_node_or(node->children[0], goto oom);
            node = node->children[0];
        }
    }
    return btree_delete0(btree, BTREE_POPFRONT, 0, NULL, NULL);
oom:
    btree->oom = true;
    return NULL;
}

BTREE_EXTERN
const void *btree_pop_max(struct btree *btree) {
    btree->oom = false;
    if (btree->root) {
        btree_cow_node_or(btree->root, goto oom);
        struct btree_node *node = btree->root;
        while (1) {
            if (node->leaf) {
                if (node->nitems > btree->min_items) {
                    size_t i = node->nitems-1;
                    btree_copy_item_into(btree, node, i, BTREE_SPARE_RETURN);
                    node->nitems--;
                    if (btree->item_free) {
                        btree->item_free(BTREE_SPARE_RETURN, btree->udata);
                    }
                    btree->count--;
                    return BTREE_SPARE_RETURN;
                }
                break;
            }
            btree_cow_node_or(node->children[node->nitems], goto oom);
            node = node->children[node->nitems];
        }
    }
    return btree_delete0(btree, BTREE_POPBACK, 0, NULL, NULL);
oom:
    btree->oom = true;
    return NULL;
}

BTREE_EXTERN
bool btree_oom(const struct btree *btree) {
    return !btree || btree->oom;
}

BTREE_EXTERN
size_t btree_count(const struct btree *btree) {
    return btree->count;
}

BTREE_EXTERN
int btree_compare(const struct btree *btree, const void *a, const void *b) {
    return _btree_compare(btree, a, b);
}

static bool btree_node_scan(const struct btree *btree, struct btree_node *node,
    bool (*iter)(const void *item, void *udata), void *udata)
{
    if (node->leaf) {
        for (size_t i = 0; i < node->nitems; i++) {
            if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
                return false;
            }
        }
        return true;
    }
    for (size_t i = 0; i < node->nitems; i++) {
        if (!btree_node_scan(btree, node->children[i], iter, udata)) {
            return false;
        }
        if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
            return false;
        }
    }
    return btree_node_scan(btree, node->children[node->nitems], iter, udata);
}

static bool btree_node_ascend(const struct btree *btree,
    struct btree_node *node,
    const void *pivot, bool (*iter)(const void *item, void *udata),
    void *udata, uint64_t *hint, int depth)
{
    bool found;
    size_t i = btree_search(btree, node, pivot, &found, hint, depth);
    if (!found) {
        if (!node->leaf) {
            if (!btree_node_ascend(btree, node->children[i], pivot, iter, udata,
                hint, depth+1))
            {
                return false;
            }
        }
    }
    for (; i < node->nitems; i++) {
        if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
            return false;
        }
        if (!node->leaf) {
            if (!btree_node_scan(btree, node->children[i+1], iter, udata)) {
                return false;
            }
        }
    }
    return true;
}

BTREE_EXTERN
bool btree_ascend_hint(const struct btree *btree, const void *pivot,
    bool (*iter)(const void *item, void *udata), void *udata, uint64_t *hint)
{
    if (btree->root) {
        if (!pivot) {
            return btree_node_scan(btree, btree->root, iter, udata);
        }
        return btree_node_ascend(btree, btree->root, pivot, iter, udata, hint,
            0);
    }
    return true;
}

BTREE_EXTERN
bool btree_ascend(const struct btree *btree, const void *pivot,
    bool (*iter)(const void *item, void *udata), void *udata)
{
    return btree_ascend_hint(btree, pivot, iter, udata, NULL);
}

static bool btree_node_reverse(const struct btree *btree,
    struct btree_node *node,
    bool (*iter)(const void *item, void *udata), void *udata)
{
    if (node->leaf) {
        size_t i = node->nitems - 1;
        while (1) {
            if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
                return false;
            }
            if (i == 0) break;
            i--;
        }
        return true;
    }
    if (!btree_node_reverse(btree, node->children[node->nitems], iter,
        udata))
    {
        return false;
    }
    size_t i = node->nitems - 1;
    while (1) {
        if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
            return false;
        }
        if (!btree_node_reverse(btree, node->children[i], iter, udata)) {
            return false;
        }
        if (i == 0) break;
        i--;
    }
    return true;
}

static bool btree_node_descend(const struct btree *btree,
    struct btree_node *node, const void *pivot,
    bool (*iter)(const void *item, void *udata),
    void *udata, uint64_t *hint, int depth)
{
    bool found;
    size_t i = btree_search(btree, node, pivot, &found, hint, depth);
    if (!found) {
        if (!node->leaf) {
            if (!btree_node_descend(btree, node->children[i], pivot, iter,
                udata, hint, depth+1))
            {
                return false;
            }
        }
        if (i == 0) {
            return true;
        }
        i--;
    }
    while(1) {
        if (!iter(btree_get_item_at((void*)btree, node, i), udata)) {
            return false;
        }
        if (!node->leaf) {
            if (!btree_node_reverse(btree, node->children[i], iter, udata)) {
                return false;
            }
        }
        if (i == 0) break;
        i--;
    }
    return true;
}

BTREE_EXTERN
bool btree_descend_hint(const struct btree *btree, const void *pivot,
    bool (*iter)(const void *item, void *udata), void *udata, uint64_t *hint)
{
    if (btree->root) {
        if (!pivot) {
            return btree_node_reverse(btree, btree->root, iter, udata);
        }
        return btree_node_descend(btree, btree->root, pivot, iter, udata, hint,
            0);
    }
    return true;
}

BTREE_EXTERN
bool btree_descend(const struct btree *btree, const void *pivot,
    bool (*iter)(const void *item, void *udata), void *udata)
{
    return btree_descend_hint(btree, pivot, iter, udata, NULL);
}

BTREE_EXTERN
const void *btree_min(const struct btree *btree) {
    struct btree_node *node = btree->root;
    if (!node) {
        return NULL;
    }
    while (1) {
        if (node->leaf) {
            return btree_get_item_at((void*)btree, node, 0);
        }
        node = node->children[0];
    }
}

BTREE_EXTERN
const void *btree_max(const struct btree *btree) {
    struct btree_node *node = btree->root;
    if (!node) {
        return NULL;
    }
    while (1) {
        if (node->leaf) {
            return btree_get_item_at((void*)btree, node, node->nitems-1);
        }
        node = node->children[node->nitems];
    }
}

BTREE_EXTERN
const void *btree_load(struct btree *btree, const void *item) {
    btree->oom = false;
    if (!btree->root) {
        return btree_set0(btree, item, NULL, false);
    }
    bool item_cloned = false;
    if (btree->item_clone) {
        if (!btree->item_clone(item, BTREE_SPARE_CLONE, btree->udata)) {
            goto oom;
        }
        item = BTREE_SPARE_CLONE;
        item_cloned = true;
    }
    btree_cow_node_or(btree->root, goto oom);
    struct btree_node *node = btree->root;
    while (1) {
        if (node->leaf) {
            if (node->nitems == btree->max_items) break;
            void *litem = btree_get_item_at(btree, node, node->nitems-1);
            if (_btree_compare(btree, item, litem) <= 0) break;
            btree_set_item_at(btree, node, node->nitems, item);
            node->nitems++;
            btree->count++;
            return NULL;
        }
        btree_cow_node_or(node->children[node->nitems], goto oom);
        node = node->children[node->nitems];
    }
    const void *prev = btree_set0(btree, item, NULL, true);
    if (!btree->oom) {
        return prev;
    }
oom:
    if (btree->item_free && item_cloned) {
        btree->item_free(BTREE_SPARE_CLONE, btree->udata);
    }
    btree->oom = true;
    return NULL;
}

BTREE_EXTERN
size_t btree_height(const struct btree *btree) {
    return btree->height;
}

struct btree_iter_stack_item {
    struct btree_node *node;
    int index;
};

struct btree_iter {
    struct btree *btree;
    void *item;
    bool seeked;
    bool atstart;
    bool atend;
    int nstack;
    struct btree_iter_stack_item stack[];
};

BTREE_EXTERN
struct btree_iter *btree_iter_new(const struct btree *btree) {
    size_t vsize = btree_align_size(sizeof(struct btree_iter) +
        sizeof(struct btree_iter_stack_item) * btree->height);
    struct btree_iter *iter = btree->malloc(vsize + btree->elsize);
    if (iter) {
        memset(iter, 0, vsize + btree->elsize);
        iter->btree = (void*)btree;
        iter->item = (void*)((char*)iter + vsize);
    }
    return iter;
}

BTREE_EXTERN
void btree_iter_free(struct btree_iter *iter) {
    iter->btree->free(iter);
}

BTREE_EXTERN
bool btree_iter_first(struct btree_iter *iter) {
    iter->atend = false;
    iter->atstart = false;
    iter->seeked = false;
    iter->nstack = 0;
    if (!iter->btree->root) {
        return false;
    }
    iter->seeked = true;
    struct btree_node *node = iter->btree->root;
    while (1) {
        iter->stack[iter->nstack++] = (struct btree_iter_stack_item) {
            .node = node,
            .index = 0,
        };
        if (node->leaf) {
            break;
        }
        node = node->children[0];
    }
    struct btree_iter_stack_item *stack = &iter->stack[iter->nstack-1];
    btree_copy_item_into(iter->btree, stack->node, stack->index, iter->item);
    return true;
}

BTREE_EXTERN
bool btree_iter_last(struct btree_iter *iter) {
    iter->atend = false;
    iter->atstart = false;
    iter->seeked = false;
    iter->nstack = 0;
    if (!iter->btree->root) {
        return false;
    }
    iter->seeked = true;
    struct btree_node *node = iter->btree->root;
    while (1) {
        iter->stack[iter->nstack++] = (struct btree_iter_stack_item) {
            .node = node,
            .index = node->nitems,
        };
        if (node->leaf) {
            iter->stack[iter->nstack-1].index--;
            break;
        }
        node = node->children[node->nitems];
    }
    struct btree_iter_stack_item *stack = &iter->stack[iter->nstack-1];
    btree_copy_item_into(iter->btree, stack->node, stack->index, iter->item);
    return true;
}

BTREE_EXTERN
bool btree_iter_next(struct btree_iter *iter) {
    if (!iter->seeked) {
        return btree_iter_first(iter);
    }
    struct btree_iter_stack_item *stack = &iter->stack[iter->nstack-1];
    stack->index++;
    if (stack->node->leaf) {
        if (stack->index == stack->node->nitems) {
            while (1) {
                iter->nstack--;
                if (iter->nstack == 0) {
                    iter->atend = true;
                    return false;
                }
                stack = &iter->stack[iter->nstack-1];
                if (stack->index < stack->node->nitems) {
                    break;
                }
            }
        }
    } else {
        struct btree_node *node = stack->node->children[stack->index];
        while (1) {
            iter->stack[iter->nstack++] = (struct btree_iter_stack_item) {
                .node = node,
                .index = 0,
            };
            if (node->leaf) {
                break;
            }
            node = node->children[0];
        }
    }
    stack = &iter->stack[iter->nstack-1];
    btree_copy_item_into(iter->btree, stack->node, stack->index, iter->item);
    return true;
}

BTREE_EXTERN
bool btree_iter_prev(struct btree_iter *iter) {
    if (!iter->seeked) {
        return false;
    }
    struct btree_iter_stack_item *stack = &iter->stack[iter->nstack-1];
    if (stack->node->leaf) {
        stack->index--;
        if (stack->index == -1) {
            while (1) {
                iter->nstack--;
                if (iter->nstack == 0) {
                    iter->atstart = true;
                    return false;
                }
                stack = &iter->stack[iter->nstack-1];
                stack->index--;
                if (stack->index > -1) {
                    break;
                }
            }
        }
    } else {
        struct btree_node *node = stack->node->children[stack->index];
        while (1) {
            iter->stack[iter->nstack++] = (struct btree_iter_stack_item) {
                .node = node,
                .index = node->nitems,
            };
            if (node->leaf) {
                iter->stack[iter->nstack-1].index--;
                break;
            }
            node = node->children[node->nitems];
        }
    }
    stack = &iter->stack[iter->nstack-1];
    btree_copy_item_into(iter->btree, stack->node, stack->index, iter->item);
    return true;
}


BTREE_EXTERN
bool btree_iter_seek(struct btree_iter *iter, const void *key) {
    iter->atend = false;
    iter->atstart = false;
    iter->seeked = false;
    iter->nstack = 0;
    if (!iter->btree->root) {
        return false;
    }
    iter->seeked = true;
    struct btree_node *node = iter->btree->root;
    while (1) {
        bool found;
        size_t i = btree_node_bsearch(iter->btree, node, key, &found);
        iter->stack[iter->nstack++] = (struct btree_iter_stack_item) {
            .node = node,
            .index = i,
        };
        if (found) {
            btree_copy_item_into(iter->btree, node, i, iter->item);
            return true;
        }
        if (node->leaf) {
            iter->stack[iter->nstack-1].index--;
            return btree_iter_next(iter);
        }
        node = node->children[i];
    }
}

BTREE_EXTERN
const void *btree_iter_item(struct btree_iter *iter) {
    return iter->item;
}

#ifdef BTREE_TEST_PRIVATE_FUNCTIONS
#include "tests/priv_funcs.h"
#endif
