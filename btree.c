// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "btree.h"

#ifdef __clang__
#pragma clang diagnostic ignored "-Wpadded"
#pragma clang diagnostic ignored "-Wvla"
#endif

static void *(*_malloc)(size_t) = NULL;
static void (*_free)(void *) = NULL;

#define btmalloc (_malloc?_malloc:malloc)
#define btfree (_free?_free:free)

// btree_set_allocator allows for configuring a custom allocator for
// all btree library operations. This function, if needed, should be called
// only once at startup and a prior to calling btree_new().
void btree_set_allocator(void *(malloc)(size_t), void (*free)(void*)) {
    _malloc = malloc;
    _free = free;
}

#define panic(_msg_) { \
    fprintf(stderr, "panic: %s (%s:%d)\n", (_msg_), __FILE__, __LINE__); \
    exit(1); \
}(void)(0)

struct node {
    bool leaf;
    short num_items;
    char *items;
    struct node *children[];
};

static void *get_item_at(size_t elsz, struct node *node, size_t index) {
    return node->items+elsz*index;
}

static void set_item_at(size_t elsz, struct node *node, size_t index, 
                        const void *item) 
{
    memcpy(get_item_at(elsz, node, index), item, elsz);
}

static void copy_item_into(size_t elsz, struct node *node, size_t index, 
                           void *into)
{ 
    memcpy(into, get_item_at(elsz, node, index), elsz);
}

static void copy_item(size_t elsz, struct node *node_a, size_t index_a, 
                                   struct node *node_b, size_t index_b) 
{
    memcpy(get_item_at(elsz, node_a, index_a), 
           get_item_at(elsz, node_b, index_b), elsz);
}

static void swap_item_at(size_t elsz, struct node *node, size_t index, 
                         const void *item, void *into)
{ 
    void *ptr = get_item_at(elsz, node, index);
    memcpy(into, ptr, elsz);
    memcpy(ptr, item, elsz);
}

struct group {
    struct node **nodes;
    size_t len, cap;
};

struct pool {
    struct group leaves;
    struct group branches;
};

// btree is a standard B-tree with post-set splits.
struct btree {
    int (*compare)(const void *a, const void *b, void *udata);
    void *udata;
    struct node *root;
    size_t count;
    struct pool pool;
    bool oom;
    size_t height;
    size_t max_items;
    size_t min_items;
    size_t elsize;
    void *spare;        // holds the result of sets and deletes
    void *litem;        // last load item
    struct node *lnode; // last load node
};

static struct node *node_new(struct btree *btree, bool leaf) {
    size_t sz = sizeof(struct node); 
    if (!leaf) {
        sz += sizeof(struct node*)*btree->max_items;
    }
    size_t itemsoff = sz;
    sz += btree->elsize*(btree->max_items-1);
    struct node *node = btmalloc(sz);
    if (!node) {
        return NULL;
    }
    node->leaf = leaf;
    node->num_items = 0;
    node->items = (char*)node+itemsoff;
    return node;
}

static void node_free(struct node *node) {
    if (!node->leaf) {
        for (int i = 0; i < node->num_items; i++) {
            node_free(node->children[i]);
        }
        node_free(node->children[node->num_items]);
    }
    btfree(node);
}

static struct node *gimme_node(struct group *group) {
    if (group->len == 0) panic("out of nodes");
    return group->nodes[--group->len];
}

static struct node *gimme_leaf(struct btree *btree) {
    return gimme_node(&btree->pool.leaves);
}

static struct node *gimme_branch(struct btree *btree) {
    return gimme_node(&btree->pool.branches);
}

static bool grow_group(struct group *group) {
    size_t cap = group->cap?group->cap*2:1;
    struct node **nodes = btmalloc(sizeof(struct node*)*cap);
    if (!nodes) {
        return false;
    }
    memcpy(nodes, group->nodes, group->len*sizeof(struct node*));
    btfree(group->nodes);
    group->nodes = nodes;
    group->cap = cap;
    return true;
}

static void takeaway(struct btree *btree, struct node *node) {
    const size_t MAXLEN = 32;
    struct group *group;
    if (node->leaf) {
        group = &btree->pool.leaves;
    } else {
        group = &btree->pool.branches;
    }
    if (group->len == MAXLEN) {
        btfree(node);
        return;
    }
    if (group->len == group->cap) {
        if (!grow_group(group)) {
            btfree(node);
            return;
        }
    }
    group->nodes[group->len++] = node;
}

// fill_pool fills the node pool prior to inserting items. This ensures there
// is enough memory before we begin doing to things like splits and tree
// rebalancing. There needs to be at least one available leaf and N branches
// where N is equal to the height of the tree.
static bool fill_pool(struct btree *btree) {
    if (btree->pool.leaves.len == 0) {
        if (btree->pool.leaves.cap == 0) {
            if (!grow_group(&btree->pool.leaves)) {
                return false;
            }
        }
        struct node *leaf = node_new(btree, true);
        if (!leaf) {
            return false;
        }
        btree->pool.leaves.nodes[btree->pool.leaves.len++] = leaf;
    }
    while (btree->pool.branches.len < btree->height) {
        if (btree->pool.branches.len == btree->pool.branches.cap) {
            if (!grow_group(&btree->pool.branches)) {
                return false;
            }
        }
        struct node *branch = node_new(btree, false);
        if (!branch) {
            return false;
        }
        btree->pool.branches.nodes[btree->pool.branches.len++] = branch;
    }
    return true;
}

static void node_join(size_t elsize, struct node *left, struct node *right) {
    memcpy(left->items+elsize*(size_t)left->num_items,
           right->items,
           (size_t)right->num_items*elsize);
    if (!left->leaf) {
        memcpy(&left->children[left->num_items],
               &right->children[0],
               (size_t)(right->num_items+1)*sizeof(struct node*));
    }
    left->num_items += right->num_items;
}

static void node_shift_right(size_t elsize, struct node *node, size_t index) {
    memmove(node->items+elsize*(index+1), 
            node->items+elsize*index,
            ((size_t)node->num_items-index)*elsize);
    if (!node->leaf) {
        memmove(&node->children[index+1],
                &node->children[index],
                ((size_t)node->num_items-index+1)*sizeof(struct node*));
    }
    node->num_items++;
}

static void node_shift_left(size_t elsize, struct node *node, size_t index, 
                            bool for_merge) 
{
    memmove(node->items+elsize*index, 
            node->items+elsize*(index+1),
            ((size_t)node->num_items-index)*elsize);
    if (!node->leaf) {
        if (for_merge) {
            index++;
        }
        memmove(&node->children[index],
                &node->children[index+1],
                ((size_t)node->num_items-index+1)*sizeof(struct node*));
    }
    node->num_items--;
}

// btree_new returns a new B-tree. 
// Param `elsize` is the size of each element in the tree. Every element that
// is inserted, deleted, or searched will be this size.
// Param `max_items` is the maximum number of items per node. Setting this to
// zero will default to 256. The max is 4096.
// Param `compare` is a function that compares items in the tree. See the 
// qsort stdlib function for an example of how this function works.
// The btree must be freed with btree_free(). 
struct btree *btree_new(size_t elsize, size_t max_items,
                        int (*compare)(const void *a, const void *b, 
                                       void *udata),
                        void *udata)
{
    if (max_items == 0) {
        max_items = 256;
    } else {
        if (max_items % 2 == 1) max_items--;
        if (max_items < 4) max_items = 4;
        if (max_items > 4096) max_items = 4096;
    }
    if (elsize == 0) panic("elsize is zero");
    if (compare == NULL) panic("compare is null");
    size_t sz = sizeof(struct btree)+elsize;
    struct btree *btree = btmalloc(sz);
    if (!btree) {
        return NULL;
    }
    memset(btree, 0, sizeof(struct btree));
    btree->compare = compare;
    btree->max_items = max_items;
    btree->min_items = btree->max_items*40/100;
    btree->elsize = elsize;
    btree->spare = ((char*)btree)+sizeof(struct btree);
    btree->udata = udata;
    return btree;
}

static void release_pool(struct btree *btree) {
    for (size_t i = 0; i < btree->pool.leaves.len; i++) {
        btfree(btree->pool.leaves.nodes[i]);
    }
    btfree(btree->pool.leaves.nodes);
    for (size_t i = 0; i < btree->pool.branches.len; i++) {
        btfree(btree->pool.branches.nodes[i]);
    }
    btfree(btree->pool.branches.nodes);
    memset(&btree->pool, 0, sizeof(struct pool));
}

// btree_free frees the btree. The items in the btree are not touched, so if
// you need to free those then do so prior to calling this function.
void btree_free(struct btree *btree) {
    if (btree->root) {
        node_free(btree->root);
    }
    release_pool(btree);
    btfree(btree);
}

// btree_height returns the height of the btree.
size_t btree_height(struct btree *btree) {
    return btree->height;
}

// btree_count returns the number of items in the btree.
size_t btree_count(struct btree *btree) {
    return btree->count;
}

static void node_split(struct btree *btree, struct node *node, 
                       struct node **right, void **median, bool lean_left) 
{
    int mid;
    if (lean_left) {
        // Split so the left node has as many items as possible, leaving the
        // new right with the minimum items. This makes more space available to
        // the right node for sequential inserts and bulk loading.
        mid = (int)(btree->max_items-1-btree->min_items);
        int mdif = (node->num_items-(mid+1))-(int)btree->min_items;
        if (mdif < 0) {
            mid += mdif;
        }
    } else {
        // split so that both left and right have the same number of items.
        mid = (int)(btree->max_items-1)/2;
    }
    *median = get_item_at(btree->elsize, node, (size_t)mid);    
    *right = node->leaf ? gimme_leaf(btree) : gimme_branch(btree);
    (*right)->leaf = node->leaf;
    (*right)->num_items = node->num_items-((short)mid+1);
    memmove((*right)->items,
            node->items+(int)btree->elsize*(mid+1),
            (*right)->num_items*(int)btree->elsize);
    if (!node->leaf) {
        for (int i = 0; i <= (*right)->num_items; i++) {
            (*right)->children[i] = node->children[mid+1+i];
        }
    }
    node->num_items = (short)mid;
}

static int node_find(struct btree *btree, struct node *node, void *key, 
                     bool *found, uint64_t *hint, int depth) 
{
    int low = 0;
    int high = node->num_items-1;
    if (hint && depth < 8) {
        int index = ((uint8_t*)hint)[depth];
        if (index > 0) {
            if (index > node->num_items-1) {
                index = node->num_items-1;
            }
            void *item = get_item_at(btree->elsize, node, (size_t)index);
            int cmp = btree->compare(key, item, btree->udata);
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
        void *item = get_item_at(btree->elsize, node, (size_t)mid);
        int cmp = btree->compare(key, item, btree->udata);
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

static bool node_set(struct btree *btree, struct node *node, void *item, 
                     bool lean_left, uint64_t *hint, int depth) 
{
    bool found = false;
    int i = node_find(btree, node, item, &found, hint, depth);
    if (found) {
        swap_item_at(btree->elsize, node, (size_t)i, item, btree->spare);
        return true;
    }
    if (node->leaf) {
        node_shift_right(btree->elsize, node, (size_t)i);
        set_item_at(btree->elsize, node, (size_t)i, item);
        return false;
    }
    if (node_set(btree, node->children[i], item, lean_left, hint, depth+1)) {
        return true;
    }
    if ((size_t)node->children[i]->num_items == (btree->max_items-1)) {
        void *median = NULL;
        struct node *right = NULL;
        node_split(btree, node->children[i], &right, &median, lean_left);
        node_shift_right(btree->elsize, node, (size_t)i);
        set_item_at(btree->elsize, node, (size_t)i, median);
        node->children[i+1] = right;
    }
    return false;
}

static void *btree_set_x(struct btree *btree, void *item, bool lean_left,
                         uint64_t *hint)
{
    if (!item) {
        panic("item is null");
    }
    btree->litem = NULL;
    btree->lnode = NULL;
    btree->oom = false;
    if (!fill_pool(btree)) {
        btree->oom = true;
        return NULL;
    }
    if (!btree->root) {
        btree->root = gimme_leaf(btree);
        set_item_at(btree->elsize, btree->root, 0, item);
        btree->root->num_items = 1;
        btree->count++;
        btree->height++;
        return NULL;
    }
    if (node_set(btree, btree->root, item, lean_left, hint, 0)) {
        return btree->spare;
    }
    btree->count++;
    if ((size_t)btree->root->num_items == (btree->max_items-1)) {
        void *old_root = btree->root;
        struct node *right = NULL;
        void *median = NULL;
        node_split(btree, old_root, &right, &median, lean_left);
        btree->root = gimme_branch(btree);
        btree->root->children[0] = old_root;
        set_item_at(btree->elsize, btree->root, 0, median);
        btree->root->children[1] = right;
        btree->root->num_items = 1;
        btree->height++;
    }
    return NULL;    
}

// btree_set inserts or replaces an item in the btree. If an item is replaced
// then it is returned otherwise NULL is returned. 
// The `btree_set`, `btree_set_hint`, and `btree_load` are the only btree 
// operations that allocates memory. If the system could not allocate the
// memory then NULL is returned and btree_oom() returns true.
void *btree_set(struct btree *btree, void *item) {
    return btree_set_x(btree, item, false, NULL);
}

// btree_set_hint is the same as btree_set except that an optional "hint" can 
// be provided which may make the operation quicker when done as a batch or 
// in a userspace context.
// The `btree_set`, `btree_set_hint`, and `btree_load` are the only btree 
// operations that allocates memory. If the system could not allocate the
// memory then NULL is returned and btree_oom() returns true.
void *btree_set_hint(struct btree *btree, void *item, uint64_t *hint) {
    return btree_set_x(btree, item, false, hint);
}

// btree_load is the same as btree_set but is optimized for sequential bulk 
// loading. It can be up to 10x faster than btree_set when the items are
// in exact order, but up to 25% slower when not in exact order.
// The `btree_set`, `btree_set_hint`, and `btree_load` are the only btree 
// operations that allocates memory. If the system could not allocate the
// memory then NULL is returned and btree_oom() returns true.
void *btree_load(struct btree *btree, void *item) {
    if (!item) {
        panic("item is null");
    }
    if (btree->litem && 
        btree->lnode && 
        (size_t)btree->lnode->num_items < btree->max_items-2 &&
        btree->compare(item, btree->litem, btree->udata) > 0)
    {
        set_item_at(btree->elsize, btree->lnode, 
                    (size_t)btree->lnode->num_items, item);
        btree->lnode->num_items++;
        btree->count++;
        btree->oom = false;
        return NULL;
    }
    void *prev = btree_set_x(btree, item, true, NULL);
    if (prev) {
        return prev;
    }
    struct node *node = btree->root;
    for (;;) {
        if (node->leaf) {
            btree->lnode = node;
            btree->litem = get_item_at(btree->elsize, node, 
                                       (size_t)(node->num_items-1));
            break;
        }
        node = node->children[node->num_items];
    }
    return NULL;
}

// btree_get_hint is the same as btree_get except that an optional "hint" can 
// be provided which may make the operation quicker when done as a batch or 
// in a userspace context.
void *btree_get_hint(struct btree *btree, void *key, uint64_t *hint) {
    struct node *node = btree->root;
    if (!node) {
        return NULL;
    }
    size_t elsz = btree->elsize;
    for (int depth = 0;;depth++) {
        bool found = false;
        int i = node_find(btree, node, key, &found, hint, depth);
        if (found) {
            return get_item_at(elsz, node, (size_t)i);
        }
        if (node->leaf) {
            return NULL;
        }
        node = node->children[i];
    }
}

// btree_get returns the item based on the provided key. If the item is not
// found then NULL is returned.
void *btree_get(struct btree *btree, void *key) {
    return btree_get_hint(btree, key, NULL);
}

enum delact {
    DELKEY, POPFRONT, POPBACK, POPMAX,
};

static bool node_delete(struct btree *btree, struct node *node, enum delact act, 
                        size_t index, void *key,
                        int (*compare)(const void *a, const void *b, 
                                       void *udata), 
                        void *prev, uint64_t *hint, int depth)
{
    int i = 0;
    bool found = false;
    switch (act) {
    case POPMAX:
        i = node->num_items-1;
        found = true;
        break;
    case POPFRONT:
        i = 0;
        found = node->leaf;
        break;
    case POPBACK:
        if (!node->leaf) {
            i = node->num_items;
            found = false;
        } else {
            i = node->num_items-1;
            found = true;
        }
        break;
    case DELKEY:
        i = node_find(btree, node, key, &found, hint, depth);
        break;
    }
    if (node->leaf) {
        if (found) {
            // item was found in leaf, copy its contents and delete it.
            copy_item_into(btree->elsize, node, (size_t)i, prev);
            node_shift_left(btree->elsize, node, (size_t)i, false);
            return true;
        }
        return false;
    }
    // branch
    bool deleted = false;
    if (found) {
        if (act == POPMAX) {
            // popping off the max item into into its parent branch to maintain
            // a balanced tree.
            i++;
            node_delete(btree, node->children[i], POPMAX, 0, NULL, 
                        NULL, prev, hint, depth+1);
            deleted = true;
        } else {
            // item was found in branch, copy its contents, delete it, and 
            // begin popping off the max items in child nodes.
            copy_item_into(btree->elsize, node, (size_t)i, prev);
            char tmp_item[btree->elsize]; // VLA
            node_delete(btree, node->children[i], POPMAX, 0, NULL, NULL, 
                        tmp_item, hint, depth+1);
            set_item_at(btree->elsize, node, (size_t)i, tmp_item);
            deleted = true;
        }
    } else {
        // item was not found in this branch, keep searching.
        deleted = node_delete(btree, node->children[i], act, index, key,
                              compare, prev, hint, depth+1);
    }
    if (!deleted) {
        return false;
    }
    
    if ((size_t)node->children[i]->num_items >= btree->min_items) {
        return true;
    }
    
    if (i == node->num_items) {
        i--;
    }

    struct node *left = node->children[i];
    struct node *right = node->children[i+1];

    if ((size_t)(left->num_items + right->num_items + 1) < 
        (btree->max_items-1)) 
    {
        // merge left + item + right
        copy_item(btree->elsize, left, (size_t)left->num_items, node, 
                  (size_t)i);
        left->num_items++;
        node_join(btree->elsize, left, right);
        takeaway(btree, right);
        node_shift_left(btree->elsize, node, (size_t)i, true);
    } else if (left->num_items > right->num_items) {
        // move left -> right
        node_shift_right(btree->elsize, right, 0);
        copy_item(btree->elsize, right, 0, node, (size_t)i);
        if (!left->leaf) {
            right->children[0] = left->children[left->num_items];
        }
        copy_item(btree->elsize, node, (size_t)i, left, 
                  (size_t)(left->num_items-1));
        if (!left->leaf) {
            left->children[left->num_items] = NULL;
        }
        left->num_items--;
    } else {
        // move right -> left
        copy_item(btree->elsize, left, (size_t)left->num_items, node, (size_t)i);
        if (!left->leaf) {
            left->children[left->num_items+1] = right->children[0];
        }
        left->num_items++;
        copy_item(btree->elsize, node, (size_t)i, right, 0);
        node_shift_left(btree->elsize, right, 0, false);
    }
    return deleted;
}

static void *delete_x(struct btree *btree, enum delact act, size_t index, 
                      void *key, uint64_t *hint) 
{
    if (!btree->root) {
        return NULL;
    }
    bool deleted = node_delete(btree, btree->root, act, index, key, 
                               btree->compare, btree->spare, hint, 0);
    if (!deleted) {
        return NULL;
    }
    if (btree->root->num_items == 0) {
        struct node *old_root = btree->root;
        if (!btree->root->leaf) {
            btree->root = btree->root->children[0];
        } else {
            btree->root = NULL;
        }
        takeaway(btree, old_root);
        btree->height--;
    }
    btree->count--;
    return btree->spare;
}

// btree_delete_hint is the same as btree_delete except that an optional "hint"
// can be provided which may make the operation quicker when done as a batch or 
// in a userspace context.
void *btree_delete_hint(struct btree *btree, void *key, uint64_t *hint) {
    if (!key) panic("key is null");
    return delete_x(btree, DELKEY, 0, key, hint);
}

// btree_delete removes an item from the B-tree and returns it. If the item is
// not found then NULL is returned.
void *btree_delete(struct btree *btree, void *key) {
    return btree_delete_hint(btree, key, NULL);
}

// btree_pop_min removed the minimum value
void *btree_pop_min(struct btree *btree) {
    return delete_x(btree, POPFRONT, 0, NULL, NULL);
}

// btree_pop_max removes the maximum value
void *btree_pop_max(struct btree *btree) {
    return delete_x(btree, POPBACK, 0, NULL, NULL);
}

// btree_min returns the minimum value
void *btree_min(struct btree *btree) {
    struct node *node = btree->root;
    if (!node) {
        return NULL;
    }
    for (;;) {
        if (node->leaf) {
            return get_item_at(btree->elsize, node, 0);
        }
        node = node->children[0];
    }
}

// btree_max returns the maximum value
void *btree_max(struct btree *btree) {
    struct node *node = btree->root;
    if (!node) {
        return NULL;
    }    
    for (;;) {
        if (node->leaf) {
            return get_item_at(btree->elsize, node, 
                               (size_t)(node->num_items-1));
        }
        node = node->children[node->num_items];
    }
}


static bool node_scan(struct btree *btree, struct node *node, 
                      bool (*iter)(const void *item, void *udata), 
                      void *udata) 
{
    if (node->leaf) {
        for (int i = 0; i < node->num_items; i++) {
            if (!iter(get_item_at(btree->elsize, node, (size_t)i), udata)) {
                return false;
            }
        }
        return true;
    }
    for (int i = 0; i < node->num_items; i++) {
        if (!node_scan(btree, node->children[i], iter, udata)) {
            return false;
        }
        if (!iter(get_item_at(btree->elsize, node, (size_t)i), udata)) {
            return false;
        }
    }
    return node_scan(btree, node->children[node->num_items], iter, udata);
}

static bool node_ascend(struct btree *btree, struct node *node, void *pivot, 
                        bool (*iter)(const void *item, void *udata), 
                        void *udata, uint64_t *hint) 
{
    bool found;
    int i = node_find(btree, node, pivot, &found, hint, 0);
    if (!found) {
        if (!node->leaf) {
            if (!node_ascend(btree, node->children[i], pivot, iter, udata,
                             hint)) 
            {
                return false;
            }
        }
    }
    for (; i < node->num_items; i++) {
        if (!iter(get_item_at(btree->elsize, node, (size_t)i), udata)) {
            return false;
        }
        if (!node->leaf) {
            if (!node_scan(btree, node->children[i+1], iter, udata)) {
                return false;
            }
        }
    }
    return true;
}

static bool node_reverse(struct btree *btree, struct node *node, 
                         bool (*iter)(const void *item, void *udata), 
                         void *udata) 
{
    if (node->leaf) {
		for (int i = node->num_items - 1; i >= 0; i--) {
			if (!iter(get_item_at(btree->elsize, node, (size_t)i), udata)) {
				return false;
			}
		}
		return true;
	}
	if (!node_reverse(btree, node->children[node->num_items], iter, udata)) {
		return false;
	}
	for (int i = node->num_items - 1; i >= 0; i--) {
		if (!iter(get_item_at(btree->elsize, node, (size_t)i), udata)) {
			return false;
		}
        if (!node_reverse(btree, node->children[i], iter, udata)) {
			return false;
		}
	}
	return true;
}

static bool node_descend(struct btree *btree, struct node *node, void *pivot, 
                        bool (*iter)(const void *item, void *udata), 
                        void *udata, uint64_t *hint) 
{
    bool found;
    int i = node_find(btree, node, pivot, &found, hint, 0);
    if (!found) {
        if (!node->leaf) {
            if (!node_descend(btree, node->children[i], pivot, iter, udata, 
                              hint)) 
            {
                return false;
            }
        }
        i--;
    }
    for (; i >= 0; i--) {
        if (!iter(get_item_at(btree->elsize, node,(size_t)i), udata)) {
            return false;
        }
        if (!node->leaf) {
            if (!node_reverse(btree, node->children[i], iter, udata)) {
                return false;
            }
        }
    }
    return true;
}

// btree_ascend_hint is the same as btree_ascend except that an optional
// "hint" can be provided which may make the operation quicker when done as a
// batch or in a userspace context.
bool btree_ascend_hint(struct btree *btree, void *pivot, 
                       bool (*iter)(const void *item, void *udata), 
                       void *udata, uint64_t *hint) 
{
    if (btree->root) {
        if (!pivot) {
            return node_scan(btree, btree->root, iter, udata);
        }
        return node_ascend(btree, btree->root, pivot, iter, udata, hint);
    }
    return true;
}

// Ascend the tree within the range [pivot, last]. In other words 
// `btree_ascend()` iterates over all items that are greater-than-or-equal-to
// pivot in ascending order.
// Param `pivot` can be NULL, which means all items are iterated over.
// Param `iter` can return false to stop iteration early.
// Returns false if the iteration has been stopped early.
bool btree_ascend(struct btree *btree, void *pivot, 
                  bool (*iter)(const void *item, void *udata), void *udata) 
{
    return btree_ascend_hint(btree, pivot, iter, udata, NULL);
}

// btree_descend_hint is the same as btree_descend except that an optional
// "hint" can be provided which may make the operation quicker when done as a
// batch or in a userspace context.
bool btree_descend_hint(struct btree *btree, void *pivot, 
                        bool (*iter)(const void *item, void *udata), 
                        void *udata, uint64_t *hint) 
{
    if (btree->root) {
        if (!pivot) {
            return node_reverse(btree, btree->root, iter, udata);
        }
        return node_descend(btree, btree->root, pivot, iter, udata, hint);
    }
    return true;
}

// Decend the tree within the range [pivot, first]. In other words 
// `btree_descend()` iterates over all items that are less-than-or-equal-to
// pivot in descending order.
// Param `pivot` can be NULL, which means all items are iterated over.
// Param `iter` can return false to stop iteration early.
// Returns false if the iteration has been stopped early.
bool btree_descend(struct btree *btree, void *pivot, 
                   bool (*iter)(const void *item, void *udata), void *udata) 
{
    return btree_descend_hint(btree, pivot, iter, udata, NULL);
}
////////////////////////////////////////////////////////////////////////////////

static void node_print(struct btree *btree, struct node *node, 
                       void (*print)(void *), int depth) 
{
    if (node->leaf) {
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }
        printf("[");
        for (int i = 0; i < node->num_items; i++) {
            if (i > 0) {
                printf(" ");
            }
            print(get_item_at(btree->elsize, node, (size_t)i));
        }
        printf("]\n");
    } else {
        for (short i = 0; i < node->num_items; i++) {
            node_print(btree, node->children[i], print, depth+1);
            for (int j = 0; j < depth; j++) {
                printf("  ");
            }
            print(get_item_at(btree->elsize, node, (size_t)i));
            printf("\n");
        }
        node_print(btree, node->children[node->num_items], print, depth+1);
    }
}

void btree_print(struct btree *btree, void (*print)(void *item));
void btree_print(struct btree *btree, void (*print)(void *item)) {
    if (btree->root) {
        node_print(btree, btree->root, print, 0);
    }
}

// btree_oom returns true if the last btree_insert() call failed due to the 
// system being out of memory.
bool btree_oom(struct btree *btree) {
    return btree->oom;
}

//==============================================================================
// TESTS AND BENCHMARKS
// $ cc -DBTREE_TEST btree.c && ./a.out              # run tests
// $ cc -DBTREE_TEST -O3 btree.c && BENCH=1 ./a.out  # run benchmarks
//==============================================================================
#ifdef BTREE_TEST

#ifdef __clang__
#pragma clang diagnostic ignored "-Weverything"
#endif
#pragma GCC diagnostic ignored "-Wextra"


static void node_walk(struct btree *btree, struct node *node, 
                      void (*fn)(const void *item, void *udata), void *udata) 
{
    if (node->leaf) {
        for (int i = 0; i < node->num_items; i++) {
            fn(get_item_at(btree->elsize, node, i), udata);
        }
    } else {
        for (int i = 0; i < node->num_items; i++) {
            node_walk(btree, node->children[i], fn, udata);
            fn(get_item_at(btree->elsize, node, i), udata);
        }
        node_walk(btree, node->children[node->num_items], fn, udata);
    }
}

// btree_walk visits every item in the tree.
static void btree_walk(struct btree *btree, 
                void (*fn)(const void *item, void *udata), void *udata) 
{
    if (btree->root) {
        node_walk(btree, btree->root, fn, udata);
    }
}

static size_t node_deepcount(struct node *node) {
    size_t count = node->num_items;
    if (!node->leaf) {
        for (int i = 0; i <= node->num_items; i++) {
            count += node_deepcount(node->children[i]);
        }
    }
    return count;
}

// btree_deepcount returns the number of items in the btree.
static size_t btree_deepcount(struct btree *btree) {
    if (btree->root) {
        return node_deepcount(btree->root);
    }
    return 0;
}

static bool node_saneheight(struct node *node, int height, int maxheight) {
    if (node->leaf) {
        if (height != maxheight) {
            return false;
        }
    } else {
        int i = 0;
        for (; i < node->num_items; i++) {
            if (!node_saneheight(node->children[i], height+1, maxheight)) {
                return false;
            }
        }
        if (!node_saneheight(node->children[i], height+1, maxheight)) {
            return false;
        }
    }
    return true;
}

// btree_saneheight returns true if the height of all leaves match the height
// of the btree.
static bool btree_saneheight(struct btree *btree) {
    if (btree->root) {
        return node_saneheight(btree->root, 1, btree->height);        
    }
    return true;
}

static bool node_saneprops(struct btree *btree, struct node *node, int height) {
    if (height == 1) {
        if (node->num_items < 1 || node->num_items > btree->max_items) {
            return false;
        }
    } else {
        if (node->num_items < btree->min_items || 
            node->num_items > btree->max_items) 
        {
            return false;
        }
    }
    if (!node->leaf) {
        for (int i = 0; i < node->num_items; i++) {
            if (!node_saneprops(btree, node->children[i], height+1)) {
                return false;
            }
        }
        if (!node_saneprops(btree, node->children[node->num_items], height+1)) {
            return false;
        }
    }
    return true;
}


static bool btree_saneprops(struct btree *btree) {
    if (btree->root) {
        return node_saneprops(btree, btree->root, 1);
    }
    return true;
}

struct sane_walk_ctx {
    struct btree *btree;
    const void *last;
    size_t count;
    bool bad;
};

static void sane_walk(const void *item, void *udata) {
    struct sane_walk_ctx *ctx = udata;
    if (ctx->bad) {
        return;
    }
    if (ctx->last != NULL) {
        if (ctx->btree->compare(ctx->last, item, ctx->btree->udata) >= 0) {
            ctx->bad = true;
            return;
        }
    }
    ctx->last = item;
    ctx->count++;
}

// btree_sane returns true if the entire btree and every node are valid.
// - height of all leaves are the equal to the btree height.
// - deep count matches the btree count.
// - all nodes have the correct number of items and counts.
// - all items are in order.
bool btree_sane(struct btree *btree) {
    if (!btree_saneheight(btree)) {
        fprintf(stderr, "!sane-height\n");
        return false;
    }
    if (btree_deepcount(btree) != btree->count) {
        fprintf(stderr, "!sane-count\n");
        return false;
    }
    if (!btree_saneprops(btree)) {
        fprintf(stderr, "!sane-props\n");
        return false;
    }
    struct sane_walk_ctx ctx = { .btree = btree };
    btree_walk(btree, sane_walk, &ctx);
    if (ctx.bad || (ctx.count != btree->count)) {
        fprintf(stderr, "!sane-order\n");
        return false;
    }
    return true;
}

struct slowget_at_ctx {
    struct btree *btree;
    int index;
    int count;
    void *result;
};

static bool slowget_at_iter(const void *item, void *udata) {
    struct slowget_at_ctx *ctx = udata;
    if (ctx->count == ctx->index) {
        ctx->result = (void*)item;
        return false;
    }
    ctx->count++;
    return true;
}

void *btree_slowget_at(struct btree *btree, size_t index);
void *btree_slowget_at(struct btree *btree, size_t index) {
    struct slowget_at_ctx ctx = { .btree = btree, .index = index };
    btree_ascend(btree, NULL, slowget_at_iter, &ctx);
    return ctx.result;
}


void print_int(void *item) {
    printf("%d", *(int*)item);
}

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include "btree.h"

static bool rand_alloc_fail = false;
static int rand_alloc_fail_odds = 3; // 1 in 3 chance malloc will fail.
static uintptr_t total_allocs = 0;
static uintptr_t total_mem = 0;

static void *xmalloc(size_t size) {
    if (rand_alloc_fail && rand()%rand_alloc_fail_odds == 0) {
        return NULL;
    }
    void *mem = malloc(sizeof(uintptr_t)+size);
    assert(mem);
    *(uintptr_t*)mem = size;
    total_allocs++;
    total_mem += size;
    return (char*)mem+sizeof(uintptr_t);
}

static void xfree(void *ptr) {
    if (ptr) {
        total_mem -= *(uintptr_t*)((char*)ptr-sizeof(uintptr_t));
        free((char*)ptr-sizeof(uintptr_t));
        total_allocs--;
    }
}

static void shuffle(void *array, size_t numels, size_t elsize) {
    char tmp[elsize];
    char *arr = array;
    for (size_t i = 0; i < numels - 1; i++) {
        int j = i + rand() / (RAND_MAX / (numels - i) + 1);
        memcpy(tmp, arr + j * elsize, elsize);
        memcpy(arr + j * elsize, arr + i * elsize, elsize);
        memcpy(arr + i * elsize, tmp, elsize);
    }
}

static char nothing[] = "nothing";

static int compare_ints_nudata(const void *a, const void *b) {
    return *(int*)a - *(int*)b;
}
static int compare_ints(const void *a, const void *b, void *udata) {
    assert(udata == nothing);
    return *(int*)a - *(int*)b;
}

struct iter_ctx {
    bool rev;
    struct btree *btree;
    const void *last;
    int count;
    bool bad;
};

static bool iter(const void *item, void *udata) {
    struct iter_ctx *ctx = udata;
    if (ctx->bad) {
        return false;
    }
    if (ctx->last) {
        if (ctx->rev) {
            if (ctx->btree->compare(item, ctx->last, ctx->btree->udata) >= 0) {
                ctx->bad = true;
                return false;
            }
        } else {
            if (ctx->btree->compare(ctx->last, item, ctx->btree->udata) >= 0) {
                ctx->bad = true;
                return false;
            }
        }
    }
    ctx->last = item;
    ctx->count++;
    return true;
}

static void all() {
    int seed = getenv("SEED")?atoi(getenv("SEED")):time(NULL);
    int max_items = getenv("MAX_ITEMS")?atoi(getenv("MAX_ITEMS")):6;
    int N = getenv("N")?atoi(getenv("N")):2000;
    printf("seed=%d, max_items=%d, count=%d, item_size=%zu\n", 
        seed, max_items, N, sizeof(int));
    srand(seed);

    rand_alloc_fail = true;

    int *vals;
    while(!(vals = xmalloc(sizeof(int) * N))){}

    for (int i = 0; i < N; i++) {
        vals[i] = i*10;
    }
    
    struct btree *btree = NULL;
    for (int h = 0; h < 2; h++) {
        if (btree) btree_free(btree);
        while (!(btree = btree_new(sizeof(int), max_items, compare_ints, 
                                   nothing))){}

        shuffle(vals, N, sizeof(int));
        uint64_t hint = 0;
        uint64_t *hint_ptr = h == 0 ? NULL : &hint;
        
        for (int i = 0; i < N; i++) {
            int *v;
            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(!v);
            while (true) {
                v = btree_set_hint(btree, &vals[i], hint_ptr);
                assert(!v);
                if (!btree_oom(btree)) {
                    break;
                }
            }
            while (true) {
                v = btree_set_hint(btree, &vals[i], hint_ptr);
                if (!v) {
                    assert(btree_oom(btree));
                } else {
                    assert(v && *(int*)v == vals[i]);
                    break;
                }
            }
            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(v && *(int*)v == vals[i]);
            assert(btree_count(btree) == (size_t)(i+1));
            assert(btree_sane(btree));

            // delete item
            v = btree_delete_hint(btree, &vals[i], hint_ptr);
            assert(v && *v == vals[i]);
            assert(btree_count(btree) == (size_t)(i));
            assert(btree_sane(btree));

            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(!v);

            // reinsert item
            v = btree_set_hint(btree, &vals[i], hint_ptr);
            assert(!v);
            assert(btree_count(btree) == (size_t)(i+1));
            assert(btree_sane(btree));

            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(v && *(int*)v == vals[i]);
        }
    }

    {  
        // ascend
        struct iter_ctx ctx = { .btree = btree, .rev = false };
        bool ret = btree_ascend(btree, NULL, iter, &ctx);
        assert(ret && !ctx.bad && ctx.count == N);

        for (int i = 0; i < N; i++) {
            struct iter_ctx ctx = { .btree = btree, .rev = false };
            bool ret = btree_ascend(btree, &(int){i*10}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-i);
        }

        for (int i = 0; i < N; i++) {
            struct iter_ctx ctx = { .btree = btree, .rev = false };
            bool ret = btree_ascend(btree, &(int){i*10-1}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-i);
        }

        for (int i = 0; i < N; i++) {
            struct iter_ctx ctx = { .btree = btree, .rev = false };
            bool ret = btree_ascend(btree, &(int){i*10+1}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-i-1);
        }
    }
    {  
        // decend
        struct iter_ctx ctx = { .btree = btree, .rev = true };
        bool ret = btree_descend(btree, NULL, iter, &ctx);
        assert(ret && !ctx.bad && ctx.count == N);

        for (int i = N-1, j = 0; i >= 0; i--, j++) {
            struct iter_ctx ctx = { .btree = btree, .rev = true };
            bool ret = btree_descend(btree, &(int){i*10}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-(N-i)+1);
        }

        for (int i = N-1; i >= 0; i--) {
            struct iter_ctx ctx = { .btree = btree, .rev = true };
            bool ret = btree_descend(btree, &(int){i*10+1}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-(N-i)+1);
        }

        for (int i = N-1; i >= 0; i--) {
            struct iter_ctx ctx = { .btree = btree, .rev = true };
            bool ret = btree_descend(btree, &(int){i*10-1}, iter, &ctx);
            assert(ret && !ctx.bad && ctx.count == N-(N-i));
        }
    }



    // delete all items
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        int *v = btree_delete(btree, &vals[i]);
        assert(v && *(int*)v == vals[i]);
        assert(btree_sane(btree));
    }

    // reinsert
    shuffle(vals, N, sizeof(int));
    int min, max;
    for (int i = 0; i < N; i++) {
        int *v;
        while (true) {
            v = btree_set(btree, &vals[i]);
            assert(!v);
            if (!btree_oom(btree)) {
                break;
            }
        }
        if (i == 0) {
            min = vals[i], max = vals[i];
        } else {
            if (vals[i] < min) {
                min = vals[i];
            } else if (vals[i] > max) {
                max = vals[i];
            }
        }
        assert(btree_sane(btree));
        v = btree_min(btree);
        assert(v && *(int*)v == min);
        v = btree_max(btree);
        assert(v && *(int*)v == max);
    }

    // pop-min
    for (int i = 0; i < N; i++) {
        int *v = btree_pop_min(btree);
        assert(v && *(int*)v == i*10);
        assert(btree_sane(btree));
    }

    // reinsert
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        while (true) {
            assert(!btree_set(btree, &vals[i]));
            if (!btree_oom(btree)) {
                break;
            }
        }
    }

    // pop-max
    for (int i = 0; i < N; i++) {
        int *v = btree_pop_max(btree);
        assert(v && *(int*)v == (N-i-1)*10);
        assert(btree_sane(btree));
    }

    btree_free(btree);
    
    xfree(vals);
    if (total_allocs != 0) {
        fprintf(stderr, "total_allocs: expected 0, got %lu\n", total_allocs);
        exit(1);
    }
}

#define bench(name, N, code) {{ \
    if (strlen(name) > 0) { \
        printf("%-14s ", name); \
    } \
    size_t tmem = total_mem; \
    size_t tallocs = total_allocs; \
    uint64_t bytes = 0; \
    clock_t begin = clock(); \
    for (int i = 0; i < N; i++) { \
        (code); \
    } \
    clock_t end = clock(); \
    double elapsed_secs = (double)(end - begin) / CLOCKS_PER_SEC; \
    double bytes_sec = (double)bytes/elapsed_secs; \
    printf("%d ops in %.3f secs, %.0f ns/op, %.0f op/sec", \
        N, elapsed_secs, \
        elapsed_secs/(double)N*1e9, \
        (double)N/elapsed_secs \
    ); \
    if (bytes > 0) { \
        printf(", %.1f GB/sec", bytes_sec/1024/1024/1024); \
    } \
    if (total_mem > tmem) { \
        size_t used_mem = total_mem-tmem; \
        printf(", %.2f bytes/op", (double)used_mem/N); \
    } \
    if (total_allocs > tallocs) { \
        size_t used_allocs = total_allocs-tallocs; \
        printf(", %.2f allocs/op", (double)used_allocs/N); \
    } \
    printf("\n"); \
}}

static void benchmarks() {
    int seed = getenv("SEED")?atoi(getenv("SEED")):time(NULL);
    int max_items = getenv("MAX_ITEMS")?atoi(getenv("MAX_ITEMS")):256;
    int N = getenv("N")?atoi(getenv("N")):1000000;
    printf("seed=%d, max_items=%d, count=%d, item_size=%zu\n", 
        seed, max_items, N, sizeof(int));
    srand(seed);


    int *vals = xmalloc(N * sizeof(int));
    for (int i = 0; i < N; i++) {
        vals[i] = i;
    }

    shuffle(vals, N, sizeof(int));

    struct btree *btree;
    uint64_t hint = 0;

    btree = btree_new(sizeof(int), max_items, compare_ints, nothing);
    qsort(vals, N, sizeof(int), compare_ints_nudata);
    bench("load (seq)", N, {
        btree_load(btree, &vals[i]);
    })
    btree_free(btree);

    shuffle(vals, N, sizeof(int));
    btree = btree_new(sizeof(int), max_items, compare_ints, nothing);
    bench("load (rand)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);


    btree = btree_new(sizeof(int), max_items, compare_ints, nothing);
    qsort(vals, N, sizeof(int), compare_ints_nudata);
    bench("set (seq)", N, {
        btree_set(btree, &vals[i]);
    })
    btree_free(btree);

    ////
    qsort(vals, N, sizeof(int), compare_ints_nudata);
    btree = btree_new(sizeof(int), max_items, compare_ints, nothing);
    bench("set (seq-hint)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);

    ////
    shuffle(vals, N, sizeof(int));
    btree = btree_new(sizeof(int), max_items, compare_ints, nothing);
    bench("set (rand)", N, {
        btree_set(btree, &vals[i]);
    })
    

    qsort(vals, N, sizeof(int), compare_ints_nudata);
    bench("get (seq)", N, {
        btree_get(btree, &vals[i]);
    })

    bench("get (seq-hint)", N, {
        btree_get_hint(btree, &vals[i], &hint);
    })

    shuffle(vals, N, sizeof(int));
    bench("get (rand)", N, {
        btree_get(btree, &vals[i]);
    })


    shuffle(vals, N, sizeof(int));
    bench("delete (rand)", N, {
        btree_delete(btree, &vals[i]);
    })
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        btree_set(btree, &vals[i]);
    }

    bench("min", N, {
        assert(btree_min(btree));
    })

    bench("max", N, {
        assert(btree_max(btree));
    })


    bench("pop-min", N, {
        btree_pop_min(btree);
    })

    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        btree_set(btree, &vals[i]);
    }

    bench("pop-max", N, {
        btree_pop_max(btree);
    })





    btree_free(btree);
    xfree(vals);
}



int main() {
    btree_set_allocator(xmalloc, xfree);

    if (getenv("BENCH")) {
        printf("Running btree.c benchmarks...\n");
        benchmarks();
    } else {
        printf("Running btree.c tests...\n");
        all();
        printf("PASSED\n");
    }
}

#endif
