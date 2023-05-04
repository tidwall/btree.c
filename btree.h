// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.

#ifndef BTREE_H
#define BTREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct btree;

struct btree *btree_new(size_t elsize, size_t max_items,
    int (*compare)(const void *a, const void *b, void *udata),
    void *udata);
struct btree *btree_new_with_allocator(
    void *(*malloc)(size_t), 
    void *(*realloc)(void *, size_t), 
    void (*free)(void*),
    size_t elsize, 
    size_t max_items,
    int (*compare)(const void *a, const void *b, void *udata),
    void *udata);
void btree_set_item_callbacks(struct btree *btree,
    bool (*clone)(const void *item, void *into, void *udata), 
    void (*free)(const void *item, void *udata));

bool btree_oom(const struct btree *btree);
size_t btree_height(const struct btree *btree);
size_t btree_count(const struct btree *btree);

struct btree *btree_clone(struct btree *btree);
void btree_free(struct btree *btree);
void btree_clear(struct btree *btree);

const void *btree_set(struct btree *btree, const void *item);
const void *btree_delete(struct btree *btree, const void *key);
const void *btree_load(struct btree *btree, const void *item);
const void *btree_pop_min(struct btree *btree);
const void *btree_pop_max(struct btree *btree);

const void *btree_min(const struct btree *btree);
const void *btree_max(const struct btree *btree);
const void *btree_get(const struct btree *btree, const void *key);
bool btree_ascend(const struct btree *btree, const void *pivot, 
    bool (*iter)(const void *item, void *udata), void *udata);
bool btree_descend(const struct btree *btree, const void *pivot, 
    bool (*iter)(const void *item, void *udata), void *udata);

// functions that support hints

const void *btree_set_hint(struct btree *btree, const void *item, uint64_t *hint);
const void *btree_get_hint(const struct btree *btree, const void *key, uint64_t *hint);
const void *btree_delete_hint(struct btree *btree, const void *key, uint64_t *hint);
bool btree_ascend_hint(const struct btree *btree, const void *pivot, 
    bool (*iter)(const void *item, void *udata), 
    void *udata, uint64_t *hint);
bool btree_descend_hint(const struct btree *btree, const void *pivot, 
    bool (*iter)(const void *item, void *udata), 
    void *udata, uint64_t *hint);

// DEPRECATED: use `btree_new_with_allocator`
void btree_set_allocator(void *(malloc)(size_t), void (*free)(void*));

#endif
