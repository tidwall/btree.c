#ifdef BTREE_TEST_PRIVATE_FUNCTIONS

#include <stdio.h>
#include <assert.h>

static void node_print(struct btree *btree, struct btree_node *node, 
    void (*print)(void *), size_t depth) 
{
    // for (size_t i = 0; i < depth; i++) {
    //     printf("  ");
    // }
    // printf("<%llx %llx>\n", (uint64_t)btree, (uint64_t)node);
    if (node->leaf) {
        for (size_t i = 0; i < depth; i++) {
            printf("  ");
        }
        printf("[");
        for (size_t i = 0; i < node->nitems; i++) {
            if (i > 0) {
                printf(" ");
            }
            print(btree_get_item_at(btree, node, i));
        }
        printf("]\n");
    } else {
        for (size_t i = 0; i < node->nitems; i++) {
            node_print(btree, node->children[i], print, depth+1);
            for (size_t j = 0; j < depth; j++) {
                printf("  ");
            }
            print(btree_get_item_at(btree, node, i));
            printf("\n");
        }
        node_print(btree, node->children[node->nitems], print, depth+1);
    }
}

void btree_print(struct btree *btree, void (*print)(void *item)) {
    // printf("== 0x%016llx ==\n", (uint64_t)btree);
    if (btree->root) {
        node_print(btree, btree->root, print, 0);
    }
}

static void node_walk(const struct btree *btree, struct btree_node *node, 
    void (*fn)(const void *item, void *udata), void *udata) 
{
    if (node->leaf) {
        for (size_t i = 0; i < node->nitems; i++) {
            fn(btree_get_item_at((void*)btree, node, i), udata);
        }
    } else {
        for (size_t i = 0; i < node->nitems; i++) {
            node_walk(btree, node->children[i], fn, udata);
            fn(btree_get_item_at((void*)btree, node, i), udata);
        }
        node_walk(btree, node->children[node->nitems], fn, udata);
    }
}

// btree_walk visits every item in the tree.
void btree_walk(const struct btree *btree, 
    void (*fn)(const void *item, void *udata), void *udata) 
{
    if (btree->root) {
        node_walk(btree, btree->root, fn, udata);
    }
}

static size_t node_deepcount(struct btree_node *node) {
    size_t count = node->nitems;
    if (!node->leaf) {
        for (size_t i = 0; i <= node->nitems; i++) {
            count += node_deepcount(node->children[i]);
        }
    }
    return count;
}

// btree_deepcount returns the number of items in the btree.
static size_t btree_deepcount(const struct btree *btree) {
    if (btree->root) {
        return node_deepcount(btree->root);
    }
    return 0;
}

static bool node_saneheight(struct btree_node *node, int height, 
    int maxheight)
{
    if (node->leaf) {
        if (height != maxheight) {
            return false;
        }
    } else {
        size_t i = 0;
        for (; i < node->nitems; i++) {
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
static bool btree_saneheight(const struct btree *btree) {
    if (btree->root) {
        return node_saneheight(btree->root, 1, btree->height);        
    }
    return true;
}

static bool node_saneprops(const struct btree *btree, 
    struct btree_node *node, int height)
{
    if (height == 1) {
        if (node->nitems < 1 || node->nitems > btree->max_items) {
            return false;
        }
    } else {
        if (node->nitems < btree->min_items || 
            node->nitems > btree->max_items) 
        {
            return false;
        }
    }
    if (!node->leaf) {
        for (size_t i = 0; i < node->nitems; i++) {
            if (!node_saneprops(btree, node->children[i], height+1)) {
                return false;
            }
        }
        if (!node_saneprops(btree, node->children[node->nitems], 
            height+1))
        {
            return false;
        }
    }
    return true;
}

static bool btree_saneprops(const struct btree *btree) {
    if (btree->root) {
        return node_saneprops(btree, btree->root, 1);
    }
    return true;
}

struct sane_walk_ctx {
    const struct btree *btree;
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
        if (btree_compare(ctx->btree, ctx->last, item) >= 0) {
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
bool btree_sane(const struct btree *btree) {
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

#endif // BTREE_TEST_PRIVATE_FUNCTIONS
