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
                        int (*compare)(const void *a, const void *b, 
                                       void *udata),
                        void *udata);
void btree_free(struct btree *btree);
bool btree_oom(struct btree *btree);
int btree_height(struct btree *btree);
size_t btree_count(struct btree *btree);

void *btree_set(struct btree *btree, void *item);
void *btree_get(struct btree *btree, void *key);
void *btree_delete(struct btree *btree, void *key);

void *btree_load(struct btree *btree, void *item);

void *btree_set_hint(struct btree *btree, void *item, uint64_t *hint);
void *btree_get_hint(struct btree *btree, void *key, uint64_t *hint);
void *btree_delete_hint(struct btree *btree, void *key, uint64_t *hint);

void *btree_pop_min(struct btree *btree);
void *btree_pop_max(struct btree *btree);
void *btree_min(struct btree *btree);
void *btree_max(struct btree *btree);

bool btree_ascend(struct btree *btree, void *pivot, 
                  bool (*iter)(const void *item, void *udata), void *udata);
bool btree_descend(struct btree *btree, void *pivot, 
                   bool (*iter)(const void *item, void *udata), void *udata);

void btree_set_allocator(void *(malloc)(size_t), void (*free)(void*));

#endif
