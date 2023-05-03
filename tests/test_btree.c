#include "tests.h"

void test_btree_operations(void) {
    
    int degree = DEF_DEGREE;
    int N = DEF_N;

    int *vals;
    while(!(vals = xmalloc(sizeof(int) * N))){}

    for (int i = 0; i < N; i++) {
        vals[i] = i*10;
    }

    struct btree *btree = NULL;
    for (int h = 0; h < 2; h++) {
        if (btree) btree_free(btree);

        OOM_WAIT({
            btree = btree_new_for_test(sizeof(int), degree, 
                compare_ints, nothing); 
        });

        shuffle(vals, N, sizeof(int));
        uint64_t hint = 0;
        uint64_t *hint_ptr = h == 0 ? NULL : &hint;
        
        for (int i = 0; i < N; i++) {
            const int *v;
            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(!v);
            OOM_WAIT({v = btree_set_hint(btree, &vals[i], hint_ptr);});
            assert(!v);

            OOM_WAIT({v = btree_set_hint(btree, &vals[i], hint_ptr);});
            assert(v && *(int*)v == vals[i]);

            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(v && *(int*)v == vals[i]);
            assert(btree_count(btree) == (size_t)(i+1));
            assert(btree_sane(btree));

            // delete item
            OOM_WAIT({v = btree_delete_hint(btree, &vals[i], hint_ptr);});
            assert(v && *v == vals[i]);
            assert(btree_count(btree) == (size_t)(i));
            assert(btree_sane(btree));

            // delete again, this time it will fail
            OOM_WAIT({v = btree_delete_hint(btree, &vals[i], hint_ptr);});
            assert(!v);
            assert(btree_count(btree) == (size_t)(i));
            assert(btree_sane(btree));


            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(!v);

            // reinsert item
            OOM_WAIT({v = btree_set_hint(btree, &vals[i], hint_ptr);});
            assert(!v);
            assert(btree_count(btree) == (size_t)(i+1));
            assert(btree_sane(btree));

            v = btree_get_hint(btree, &vals[i], hint_ptr);
            assert(v && *(int*)v == vals[i]);
        }
    }

    //printf("== testing ascend\n");
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

        for (int i = 0; i < N; i++) {
            struct iter_ctx ctx = { 
                .btree = btree, 
                .rev = false,
                .stop_at = 1,
            };
            bool ret = btree_ascend(btree, &i, iter, &ctx);
            assert((!ret || i==N-1) && !ctx.bad && ctx.count == 1);
        }

        for (int i = 0; i < N; i++) {
            struct iter_ctx ctx = { 
                .btree = btree, 
                .rev = false,
                .stop_at = i,
            };
            bool ret = btree_ascend(btree, NULL, iter, &ctx);
            assert((!ret || i == 0) && !ctx.bad && (ctx.count == i || i == 0));
        }
    }

    // printf("== testing descend\n");
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

        for (int i = N-1; i >= 0; i--) {
            struct iter_ctx ctx = { 
                .btree = btree, 
                .rev = true,
                .stop_at = 1,
            };
            bool ret = btree_descend(btree, &(int){i*10}, iter, &ctx);
            assert((!ret || i==0) && !ctx.bad && ctx.count == 1);
        }

        for (int i = N-1; i >= 0; i--) {
            struct iter_ctx ctx = { 
                .btree = btree, 
                .rev = true,
                .stop_at = i,
            };
            bool ret = btree_descend(btree, NULL, iter, &ctx);
            assert((!ret || i==0) && !ctx.bad && (ctx.count == i || i == 0));
        }
    }


    // delete all items
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        const int *v;
        OOM_WAIT({ v = btree_delete(btree, &vals[i]); });
        assert(v && *(int*)v == vals[i]);
        assert(btree_sane(btree));
    }

    // printf("== testing pop-min\n");

    // reinsert
    shuffle(vals, N, sizeof(int));
    int min, max;
    for (int i = 0; i < N; i++) {
        const int *v;
        OOM_WAIT({ v = btree_set(btree, &vals[i]); });
        assert(!v);
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

    for (int l = 0; l < 10; l++) {
        // reinsert with set shuffled
        shuffle(vals, N, sizeof(int));
        for (int i = 0; i < N; i++) {
            const void *v;
            OOM_WAIT({ v = btree_set(btree, &vals[i]); });
            assert(v && *(int*)v == vals[i]);
            assert(btree_count(btree) == (size_t)N);
            assert(btree_sane(btree));
        }
    }




    // pop-min
    for (int i = 0; i < N; i++) {
        const int *v;
        OOM_WAIT({ v = btree_pop_min(btree); });
        assert(v && *(int*)v == i*10);
        assert(btree_sane(btree));
    }

    // printf("== testing pop-max\n");
    // reinsert
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        const void *v;
        OOM_WAIT({ v = btree_set(btree, &vals[i]); });
        assert(!v);
    }

    // pop-max
    for (int i = 0; i < N; i++) {
        const int *v;
        OOM_WAIT({ v = btree_pop_max(btree); });
        assert(v && *(int*)v == (N-i-1)*10);
        assert(btree_sane(btree));
    }
    assert(btree_count(btree) == 0);
    assert(btree_sane(btree));

    btree_free(btree);
    xfree(vals);
}

static int nptrs = 0;

void *malloc2(size_t size) {
    nptrs++;
    return malloc(size);
}

void free2(void *ptr) {
    free(ptr);
    nptrs--;
}

// void test_btree_various(void) {
//     btree_set_allocator(malloc2, free2);
//     struct btree *btree = btree_new(sizeof(int), 4, compare_ints, nothing);
//     assert(nptrs > 0);
//     btree_free(btree);
//     assert(nptrs == 0);

//     // reset the allocator
//     init_test_allocator(rand_alloc_fail);
    
//     // int x = 1;
//     // while (1) {
//     //     while(!(btree = btree_new_for_test(sizeof(int), 4, compare_ints, nothing)));
//     //     OOM_WAIT(btree_set(btree, &x));

//     //     btree_action_descend(btree, NULL, int_keep, NULL);
//     //     bool fail = btree_oom(btree);
//     //     btree_free(btree);
//     //     if (!fail) continue;
//     //     break;
//     // }

// }



// void test_btree(void) {

//     int N = 1000;
//     int *vals = xmalloc(sizeof(int) * N);
//     for (int i = 0; i < N; i++) {
//         vals[i] = i;
//     }
//     shuffle(vals, N, sizeof(int));


//     struct btree *btree = btree_new_with_allocator(
//         xmalloc, NULL, 
//         xfree,
//         sizeof(int), 4,
//         compare_ints, 
//         // NULL,
//         nothing);
//     // btree_set_direct_comparator(btree, BTREE_COMPARE_INTS, 0);

//     for (int i = 0; i < N; i++) {
//         assert(!btree_set(btree, &vals[i]));
//         assert(btree_count(btree) == (size_t)(i+1));
//         for (int j = 0; j <= i; j++) {
//             const void *prev = btree_get(btree, &vals[j]);
//             assert(prev && *(int*)prev == vals[j]);
//         }
//         assert(btree_sane(btree));
//     }



//     xfree(vals);
//     btree_free(btree);
// }


void test_btree_load(void) {
    int degree = DEF_DEGREE;
    int N = DEF_N;

    int *vals;
    while(!(vals = xmalloc(sizeof(int) * N))){}

    for (int i = 0; i < N; i++) {
        vals[i] = i*10;
    }

    struct btree *btree = NULL;
    OOM_WAIT({
        btree = btree_new_for_test(sizeof(int), degree, 
            compare_ints, nothing); 
    });




    // reinsert with load in order
    qsort(vals, N, sizeof(int), compare_ints0);
    for (int i = 0; i < N; i++) {
        const void *v;
        OOM_WAIT({ v = btree_load(btree, &vals[i]); });
        assert(!v);
        assert(btree_count(btree) == (size_t)(i+1));
        assert(btree_sane(btree));
    }

    // delete all 
    btree_clear(btree);
    assert(btree_count(btree) == 0);
    assert(btree_sane(btree));

        // and try again
        btree_clear(btree);
        assert(btree_count(btree) == 0);
        assert(btree_sane(btree));
        // reinsert with load shuffled
        shuffle(vals, N, sizeof(int));
        for (int i = 0; i < N; i++) {
            const void *v;
            OOM_WAIT({ v = btree_load(btree, &vals[i]); });
            assert(!v);
            assert(btree_count(btree) == (size_t)(i+1));
            assert(btree_sane(btree));
        }

    for (int l = 0; l < 10; l++) {
        // reinsert with load shuffled
        shuffle(vals, N, sizeof(int));
        for (int i = 0; i < N; i++) {
            const void *v;
            OOM_WAIT({ v = btree_load(btree, &vals[i]); });
            assert(v && *(int*)v == vals[i]);
            assert(btree_count(btree) == (size_t)N);
            assert(btree_sane(btree));
        }
    }


    btree_clear(btree);
    assert(btree_count(btree) == 0);
    assert(btree_sane(btree));



    // reinsert with load sorted
    qsort(vals, N, sizeof(int), compare_ints0);
    for (int i = 0; i < N; i++) {
        const void *v;
        OOM_WAIT({ v = btree_load(btree, &vals[i]); });
        assert(!v);
        assert(btree_count(btree) == (size_t)(i+1));
        assert(btree_sane(btree));
    }

    for (int l = 0; l < 10; l++) {
        // reinsert with load sorted
        qsort(vals, N, sizeof(int), compare_ints0);
        for (int i = 0; i < N; i++) {
            const void *v;
            OOM_WAIT({ v = btree_load(btree, &vals[i]); });
            assert(v && *(int*)v == vals[i]);
            assert(btree_count(btree) == (size_t)N);
            assert(btree_sane(btree));
        }
    }


    btree_free(btree);
    xfree(vals);
}

atomic_int x2 = 0;

static void *xmalloc2(size_t size) {
    atomic_fetch_add(&x2, 1);
    return xmalloc(size);
}

static void xfree2(void *ptr) {
    atomic_fetch_sub(&x2, 1);
    xfree(ptr);
}



void test_btree_various(void) {
    btree_set_allocator(xmalloc2, xfree2);
    struct btree *btree = btree_new(sizeof(int), 10, compare_ints, nothing);
    assert(atomic_load(&x2) > 0);
    btree_free(btree);
    assert(atomic_load(&x2) == 0);
    btree_set_allocator(NULL, NULL);
    btree = btree_new(sizeof(int), 10, compare_ints, nothing);
    assert(atomic_load(&x2) == 0);
    btree_free(btree);

    btree = btree_new_for_test(sizeof(int), 0, compare_ints, nothing);
    for (int i = 0; i < 1000; i++) assert(!btree_set(btree, &i));
    for (int i = 0; i < 1000; i++) assert(btree_get(btree, &i));
    assert(btree_sane(btree));
    btree_free(btree);

    btree = btree_new_for_test(sizeof(int), 1, compare_ints, nothing);
    for (int i = 0; i < 1000; i++) assert(!btree_set(btree, &i));
    for (int i = 0; i < 1000; i++) assert(btree_get(btree, &i));
    assert(btree_sane(btree));
    btree_free(btree);

    btree = btree_new_for_test(sizeof(int), 2, compare_ints, nothing);
    for (int i = 0; i < 1000; i++) assert(!btree_set(btree, &i));
    for (int i = 0; i < 1000; i++) assert(btree_get(btree, &i));
    assert(btree_sane(btree));
    btree_free(btree);

    btree = btree_new_for_test(sizeof(int), 300, compare_ints, nothing);
    assert(btree);
    assert(!btree_min(btree));
    assert(!btree_max(btree));
    assert(btree_ascend(btree, NULL, NULL, NULL));
    assert(btree_descend(btree, NULL, NULL, NULL));
    for (int i = 0; i < 1000; i++) assert(!btree_set(btree, &i));
    for (int i = 0; i < 1000; i++) assert(btree_get(btree, &i));
    assert(btree_height(btree) > 0);
    assert(btree_sane(btree));
    btree_free(btree);

}

void pair_print(void *item) {
    struct pair *pair = item;
    printf("(%d:%d)", pair->key, pair->val);
}

void test_btree_delete(void) {
    int N = 1000;

    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));
    for (int i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = 1;
    }

    struct btree *btree;
    OOM_WAIT({btree = btree_new_for_test(sizeof(struct pair), 4, 
        compare_pairs, nothing);});

    // shuffle(pairs, N, sizeof(struct pair));

    for (int i = 0; i < N; i++) {
        const void *prev;
        OOM_WAIT( { prev = btree_set(btree, &pairs[i]); } );
        assert(!prev);
    }

    // btree_print(btree, pair_print);

    // delete every other obj in btree2
    for (int i = 0; i < N; i += 2) {
        const void *v;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v && ((struct pair*)v)->key == pairs[i].key);
    }

    // get every other obj
    for (int i = 0; i < N; i += 2) {
        const void *v;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(!v);
    }


    btree_free(btree);
    xfree(pairs);
}

int main(int argc, char **argv) {
    do_chaos_test(test_btree_operations);
    do_chaos_test(test_btree_load);
    do_chaos_test(test_btree_delete);
    do_test(test_btree_various);
    return 0;
}
