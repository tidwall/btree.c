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

static int compare_cstrs(const void *a, const void *b, void *udata) {
    (void)udata;
    return strcmp(*(char**)a, *(char**)b);
}

int main() {
    int seed = getenv("SEED")?atoi(getenv("SEED")):time(NULL);
    int degree = getenv("DEGREE")?atoi(getenv("DEGREE")):128;
    int N = getenv("N")?atoi(getenv("N")):1000000;
    printf("seed=%d, degree=%d, count=%d, item_size=%zu\n", 
        seed, degree, N, sizeof(int));
    srand(seed);

    init_test_allocator(false);

    int *vals = xmalloc(N * sizeof(int));
    for (int i = 0; i < N; i++) {
        vals[i] = i;
    }

    shuffle(vals, N, sizeof(int));

    struct btree *btree;
    uint64_t hint = 0;

    btree = btree_new_for_test(sizeof(int), degree, compare_ints2, NULL);
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    bench("load (seq)", N, {
        btree_load(btree, &vals[i]);
    })
    btree_free(btree);

    shuffle(vals, N, sizeof(int));
    btree = btree_new_for_test(sizeof(int), degree, compare_ints2, NULL);
    bench("load (rand)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);


    btree = btree_new_for_test(sizeof(int), degree, compare_ints2, NULL);
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    bench("set (seq)", N, {
        btree_set(btree, &vals[i]);
    })
    btree_free(btree);

    ////
    qsort(vals, N, sizeof(int), compare_ints2_nudata);
    btree = btree_new_for_test(sizeof(int), degree, compare_ints2, NULL);
    bench("set (seq-hint)", N, {
        btree_set_hint(btree, &vals[i], &hint);
    })
    btree_free(btree);

    ////
    shuffle(vals, N, sizeof(int));
    btree = btree_new_for_test(sizeof(int), degree, compare_ints2, NULL);
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


    btree_free(btree);
    xfree(vals);

    cleanup_test_allocator();




// if (1) {
//     int N = 1000000;
//     int *vals = xmalloc(sizeof(int) * N);
//     for (int i = 0; i < N; i++) {
//         vals[i] = i;
//     }
//     shuffle(vals, N, sizeof(int));

//     int key[1];
//     memset(key, 1, sizeof(key));

//     struct btree_alt *btree = btree_new_with_allocator_alt(
//         xmalloc, NULL, xfree,
//         sizeof(key), 0,
//         NULL,
//         // compare_ints2, 
//         NULL);
//     btree_set_direct_comparator(btree, BTREE_COMPARE_INTS, 0);


//     printf("\n== alt ints ==\n");
//     shuffle(vals, N, sizeof(int));
//     bench("set (rand)", N, {
//         key[0] = vals[i];
//         btree_set_alt(btree, &key);
//     })

//     shuffle(vals, N, sizeof(int));
//     bench("get (rand)", N, {
//         key[0] = vals[i];
//         btree_get_alt(btree, &key);
//     })



// }


// if (1) {
//     int N = 1000000;
//     char **vals = xmalloc(sizeof(char*) * N);
//     for (int i = 0; i < N; i++) {
//         vals[i] = rand_key(16);
//     }

//     struct btree_alt *btree = btree_new_with_allocator_alt(
//         xmalloc, NULL, xfree,
//         sizeof(char*), 0, // max_items/2, 
//         compare_cstrs, //  NULL, 
//         NULL);

//     printf("\n== alt cstrs ==\n");
//     shuffle(vals, N, sizeof(char*));
//     bench("set (rand)", N, {
//         btree_set_alt(btree, &vals[i]);
//     })

//     shuffle(vals, N, sizeof(char*));
//     bench("get (rand)", N, {
//         btree_get_alt(btree, &vals[i]);
//     })




// }


    return 0;
}