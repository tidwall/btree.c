#include "tests.h"


void test_btree_operations(void) {
    
    int max_items = DEF_MAX_ITEMS;
    int N = DEF_N;

    int *vals;
    while(!(vals = xmalloc(sizeof(int) * N))){}

    for (int i = 0; i < N; i++) {
        vals[i] = i*10;
    }
    
    struct btree *btree = NULL;
    for (int h = 0; h < 2; h++) {
        if (btree) btree_free(btree);

        while (!(btree = btree_new_for_test(sizeof(int), max_items, 
            compare_ints, nothing)));

        shuffle(vals, N, sizeof(int));
        uint64_t hint = 0;
        uint64_t *hint_ptr = h == 0 ? NULL : &hint;
        
        for (int i = 0; i < N; i++) {
            const int *v;
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
            assert((!ret || i==0) && !ctx.bad && ctx.count == i || i == 0);
        }
    }



    // delete all items
    shuffle(vals, N, sizeof(int));
    for (int i = 0; i < N; i++) {
        const int *v = btree_delete(btree, &vals[i]);
        assert(v && *(int*)v == vals[i]);
        assert(btree_sane(btree));
    }

    // printf("== testing pop-min\n");

    // reinsert
    shuffle(vals, N, sizeof(int));
    int min, max;
    for (int i = 0; i < N; i++) {
        const int *v;
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
        while (true) {
            const int *v = btree_pop_min(btree);
            if (btree_oom(btree)) continue;
            assert(v && *(int*)v == i*10);
            assert(btree_sane(btree));
            break;
        }
    }

    // printf("== testing pop-max\n");
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
        while (true) {
            const int *v = btree_pop_max(btree);
            if (btree_oom(btree)) continue;
            assert(v && *(int*)v == (N-i-1)*10);
            assert(btree_sane(btree));
            break;
        }
    }


    btree_free(btree);
    xfree(vals);
}

static void test_btree_actions(void) {
    int max_items = DEF_MAX_ITEMS;
    int N = DEF_N;
    
    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));

    for (int i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = i;
    }

    // qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    
    struct btree *btree;
    while(!(btree = btree_new_for_test(sizeof(struct pair), max_items, 
        compare_pairs, nothing)));

    // printf("== testing action ascend\n");
    shuffle(pairs, N, sizeof(struct pair));
    for (int i = 0; i < N; i++) {
        OOM_WAIT(btree_set(btree, &pairs[i]));
    }
    // test that all items exist and are in order, BTREE_NONE
    struct pair_keep_ctx ctx = { 0 };
    OOM_WAIT(btree_action_ascend(btree, NULL, pair_keep, &ctx));
    assert(ctx.count == N);
    assert(btree_sane(btree));

    // test items exist at various pivot points and are in order, BTREE_NONE
    qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    for (int i = 2 ; i < 16; i++) {
        memset(&ctx, 0, sizeof(struct pair_keep_ctx));
        OOM_WAIT(btree_action_ascend(btree, &pairs[N/i], pair_keep, &ctx));
        assert(ctx.count == N-N/i);
        assert(btree_sane(btree));
    }

    // update all item values, BTREE_UPDATE
    OOM_WAIT(btree_action_ascend(btree, NULL, pair_update, NULL));
    OOM_WAIT(btree_action_ascend(btree, &pairs[N/2], pair_update, NULL));
    int half = N/2;
    btree_ascend(btree, NULL, pair_update_check, &half);
    assert(btree_sane(btree));

    // delete all items, BTREE_DELETE
    OOM_WAIT(btree_action_ascend(btree, NULL, pair_delete, NULL));
    assert(btree_count(btree) == 0);
    assert(btree_sane(btree));

    // delete items at various pivot points, BTREE_DELETE
    for (int i = 2 ; i < 16; i++) {
        qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
        for (int i = 0; i < N; i++) {
            OOM_WAIT(btree_set(btree, &pairs[i]));
        }
        assert(btree_count(btree) == (size_t)N);
        OOM_WAIT(btree_action_ascend(btree, &pairs[N/i], pair_delete, NULL));
        assert(btree_count(btree) == (size_t)(N/i));
        assert(btree_sane(btree));
    }


    qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    for (int i = 0; i < N; i++) {
        OOM_WAIT(btree_set(btree, &pairs[i]));
    }

    // cycle the BTREE_NONE, BTREE_UPDATE, BTREE_DELETE
    int cycle = 0;
    OOM_WAIT(btree_action_ascend(btree, NULL, pair_cycle, &cycle));
    assert(btree_count(btree) == (size_t)(N-N/3));
    assert(btree_sane(btree));
    for (int i = 0; i < N; i++) {
        const struct pair *pair = btree_get(btree, &pairs[i]);
        switch (i % 3) {
        case 0:
            assert(pair && pair->key == pair->val);
            break;
        case 1:
            assert(pair && pair->key == pair->val-1);
            break;
        case 2:
            assert(!pair);
            break;            
        }
    }

    // printf("== testing action descend\n");
    // do the same stuff as the ascend test, but in reverse
    qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    for (int i = 0; i < N; i++) {
        OOM_WAIT(btree_set(btree, &pairs[i]));
    }

    // test that all items exist and are in order, BTREE_NONE
    memset(&ctx, 0, sizeof(struct pair_keep_ctx));
    // printf(">>%d<<\n", pairs[N/2].key);
    OOM_WAIT(btree_action_descend(btree, NULL, pair_keep_desc, &ctx));
    assert(ctx.count == N);
    assert(btree_sane(btree));

    // test items exist at various pivot points and are in order, BTREE_NONE
    qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    for (int i = 2 ; i < 16; i++) {
        memset(&ctx, 0, sizeof(struct pair_keep_ctx));
        OOM_WAIT(btree_action_descend(btree, &pairs[N/i], pair_keep_desc, &ctx));
        assert(ctx.count == N/i+1);
        assert(btree_sane(btree));
    }

    // update all item values, BTREE_UPDATE
    OOM_WAIT(btree_action_descend(btree, NULL, pair_update, NULL));
    OOM_WAIT(btree_action_descend(btree, &pairs[N/2], pair_update, NULL));
    half = N/2;
    btree_descend(btree, NULL, pair_update_check_desc, &half);
    assert(btree_sane(btree));

    // delete all items, BTREE_DELETE
    OOM_WAIT(btree_action_descend(btree, NULL, pair_delete, NULL));
    assert(btree_count(btree) == 0);
    assert(btree_sane(btree));

    // delete items at various pivot points, BTREE_DELETE
    for (int i = 2 ; i < 16; i++) {
        qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
        for (int i = 0; i < N; i++) {
            OOM_WAIT(btree_set(btree, &pairs[i]));
        }
        assert(btree_count(btree) == (size_t)(N));
        OOM_WAIT(btree_action_descend(btree, &pairs[N/i], pair_delete, NULL));
        assert(btree_count(btree) == (size_t)(N-(N/i+1)));
        assert(btree_sane(btree));
    }

    qsort(pairs, N, sizeof(struct pair), compare_pairs_nudata);
    for (int i = 0; i < N; i++) {
        OOM_WAIT(btree_set(btree, &pairs[i]));
    }

    // cycle the BTREE_NONE, BTREE_UPDATE, BTREE_DELETE
    cycle = 0;
    OOM_WAIT(btree_action_descend(btree, NULL, pair_cycle, &cycle));
    assert(btree_count(btree) == (size_t)(N-N/3));
    assert(btree_sane(btree));
    for (int i = N-1, j = 0; i >= 0; i--, j++) {
        const struct pair *pair = btree_get(btree, &pairs[i]);
        switch (j % 3) {
        case 0:
            assert(pair && pair->key == pair->val);
            break;
        case 1:
            assert(pair && pair->key == pair->val-1);
            break;
        case 2:
            assert(!pair);
            break;            
        }
    }

    xfree(pairs);
    btree_free(btree);
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

enum btree_action int_keep(void *item, void *udata) {
    (void)udata; (void)item;
    return BTREE_NONE;
}


void test_btree_various(void) {
    btree_set_allocator(malloc2, free2);
    struct btree *btree = btree_new(sizeof(int), 4, compare_ints, nothing);
    assert(nptrs > 0);
    btree_free(btree);
    assert(nptrs == 0);

    // reset the allocator
    init_test_allocator(rand_alloc_fail);
    
    // int x = 1;
    // while (1) {
    //     while(!(btree = btree_new_for_test(sizeof(int), 4, compare_ints, nothing)));
    //     OOM_WAIT(btree_set(btree, &x));

    //     btree_action_descend(btree, NULL, int_keep, NULL);
    //     bool fail = btree_oom(btree);
    //     btree_free(btree);
    //     if (!fail) continue;
    //     break;
    // }

}

int main(int argc, char **argv) {
    do_chaos_test(test_btree_operations);
    do_chaos_test(test_btree_actions);
    do_chaos_test(test_btree_various);
    return 0;
}
