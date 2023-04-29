#include "tests.h"
#include "../btree.h"

static int compare_ints2(const void *a, const void *b, void *udata) {
    return *(int*)a - *(int*)b;
}

static int compare_ints2_nudata(const void *a, const void *b) {
    return *(int*)a - *(int*)b;
}

static bool simple_iter2(const void *item, void *udata) {
    (void)item; (void)udata;
    return true;
}

static enum btree_action del_asc_odds2(void *item, void *udata) {
    (void)item;
    int count = *(int*)udata;
    count++;
    *(int*)udata = count;
    if ((count & 1) == 1) {
        return BTREE_DELETE;
    } else {
        return BTREE_NONE;
    }
}


int main() {
    int seed = getenv("SEED")?atoi(getenv("SEED")):time(NULL);
    int max_items = getenv("MAX_ITEMS")?atoi(getenv("MAX_ITEMS")):256;
    int N = getenv("N")?atoi(getenv("N")):1000000;
    printf("seed=%d, max_items=%d, count=%d, item_size=%zu\n", 
        seed, max_items, N, sizeof(int));
    srand(seed);

    init_test_allocator(false);

    int *vals = xmalloc(N * sizeof(int));
    for (int i = 0; i < N; i++) {
        vals[i] = i;
    }

    shuffle(vals, N, sizeof(int));

    struct btree *btree;
    uint64_t hint = 0;

    btree = btree_new_for_test(sizeof(int), max_items, compare_ints2, NULL);
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    bench("load (seq)", N, {
        btree_load(btree, &vals[i]);
    })
    btree_free(btree);

    shuffle(vals, N, sizeof(int));
    btree = btree_new_for_test(sizeof(int), max_items, compare_ints2, NULL);
    bench("load (rand)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);


    btree = btree_new_for_test(sizeof(int), max_items, compare_ints2, NULL);
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    bench("set (seq)", N, {
        btree_set(btree, &vals[i]);
    })
    btree_free(btree);

    ////
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    btree = btree_new_for_test(sizeof(int), max_items, compare_ints2, NULL);
    bench("set (seq-hint)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);

    ////
    shuffle(vals, N, sizeof(int));
    btree = btree_new_for_test(sizeof(int), max_items, compare_ints2, NULL);
    bench("set (rand)", N, {
        btree_set(btree, &vals[i]);
    })
    

    qsort(vals, N, sizeof(int), compare_ints2_nudata);
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

    bench("ascend", N, {
        btree_ascend(btree, NULL, simple_iter2, NULL);
        break;
    })

    bench("descend", N, {
        btree_descend(btree, NULL, simple_iter2, NULL);
        break;
    })

    bench("pop-min", N, {
        btree_pop_min(btree);
    })

    // -- pop last items from tree -- 
    // reinsert
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        btree_set(btree, &vals[i]);
    }
    bench("pop-max", N, {
        btree_pop_max(btree);
    })

    // -- delete all odd value items from the tree -- 
    // reinsert
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        btree_set(btree, &vals[i]);
    }
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    int count = 0;
    bench("asc-del-odds", N, {
        btree_action_ascend(btree, NULL, del_asc_odds2, &count);
        break;
    });

    // reinsert
    for (int i = 0; i < N; i++) {
        btree_set(btree, &vals[i]);
    }
    count = 0;
    bench("desc-del-odds", N, {
        btree_action_descend(btree, NULL, del_asc_odds2, &count);
        break;
    });

    

    btree_free(btree);
    xfree(vals);

    cleanup_test_allocator();

    return 0;
}