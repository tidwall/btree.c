#include <pthread.h>
#include "tests.h"

void pair_print(void *item) {
    printf("%d:%d", ((struct pair*)item)->key, ((struct pair*)item)->val);
}

bool pair_clone(const void *item, void *into, void *udata) {
    (void)udata;
    void *n = xmalloc(1);
    if (n) {
        memcpy(into, item, sizeof(struct pair));
        xfree(n);
        return true;
    }
    return false;
}

void pair_free(const void *item, void *udata) {
    (void)udata; (void)item;
}

void test_clone_items_withcallbacks(bool withcallbacks) {    
    size_t N = 10000;
    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));

    for (size_t i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = 0;
    }
    
    shuffle(pairs, N, sizeof(struct pair));

    struct btree *btree; 
    
    for (int h = 0; h < 100; h++) {
        while(!(btree = btree_new_for_test(sizeof(struct pair), 4, 
            compare_pairs, nothing)));
        if (withcallbacks) {
            btree_set_item_callbacks(btree, pair_clone, pair_free);
        }
        for (size_t i = 0; i < N; i++) {
            const void *v;
            OOM_WAIT( { v = btree_load(btree, &pairs[i]); } );
            assert(!v);
        }
        assert(btree_sane(btree));
        btree_free(btree);
    }

    while(!(btree = btree_new_for_test(sizeof(struct pair), 4, 
            compare_pairs, nothing)));
    if (withcallbacks) {
        btree_set_item_callbacks(btree, pair_clone, pair_free);
    }

    for (size_t i = 0; i < N; i++) {
        const void *prev;
        OOM_WAIT( { prev = btree_load(btree, &pairs[i]); } );
        assert(!prev);
    }

    assert(btree_count(btree) == N);
    assert(btree_sane(btree));

    for (size_t i = 0; i < N; i++) {
        struct pair pair2 = pairs[i];
        pair2.val++;
        const void *prev;
        OOM_WAIT( { prev = btree_set(btree, &pair2); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 0);
    }

    assert(btree_count(btree) == N);
    assert(btree_sane(btree));


    for (size_t i = 0; i < N; i += 2) {
        const void *v;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v && ((struct pair*)v)->key == pairs[i].key);
    }



    btree_free(btree);
    xfree(pairs);
}


void test_clone_items(void) {
    test_clone_items_withcallbacks(true);
}

void test_clone_items_nocallbacks(void) {
    test_clone_items_withcallbacks(false);
}

void test_clone_pairs_diverge_withcallbacks(bool withcallbacks) {

    size_t N = 10000;
    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));

    for (size_t i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = 0;
    }

    shuffle(pairs, N, sizeof(struct pair));

    struct btree *btree; // used for oom_loop
    struct btree *btree1;
    while(!(btree1 = btree_new_for_test(sizeof(struct pair), 4, 
        compare_pairs, nothing)));

    if (withcallbacks) {
        btree_set_item_callbacks(btree1, pair_clone, pair_free);
    }
    for (size_t i = 0; i < N; i++) {
        const void *prev;
        btree = btree1;
        OOM_WAIT( { prev = btree_set(btree1, &pairs[i]); } );
        assert(!prev);
    }
    assert(btree_count(btree1) == N);
    assert(btree_sane(btree1));

    // clone the btree1 into btree2

    struct btree *btree2;
    while (!(btree2 = btree_clone(btree1)));


    // update btree1 to have val = 1
    
    for (size_t i = 0; i < N; i++) {
        struct pair pair1 = pairs[i];
        pair1.val = 1;
        const void *prev;
        btree = btree1;
        OOM_WAIT( { prev = btree_set(btree1, &pair1); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 0);
    }

    // update btree2 to have val = 2

    for (size_t i = 0; i < N; i++) {
        struct pair pair2 = pairs[i];
        pair2.val = 2;
        const void *prev;
        btree = btree2;
        OOM_WAIT( { prev = btree_set(btree2, &pair2); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 0);
    }

    for (size_t i = 0; i < N; i++) {
        const void *v;
        btree = btree1;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 1);
    }

    for (size_t i = 0; i < N; i++) {
        const void *v;
        btree = btree2;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 2);
    }

    assert(btree_sane(btree1));
    assert(btree_sane(btree2));
    assert(btree_count(btree1) == N);
    assert(btree_count(btree2) == N);


    for (size_t i = 0; i < N; i += 2) {
        const void *prev;
        btree = btree1;
        OOM_WAIT( { prev = btree_delete(btree, &pairs[i]); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 1);
    }

    for (size_t i = 0; i < N; i += 2) {
        const void *prev;
        btree = btree2;
        OOM_WAIT( { prev = btree_delete(btree, &pairs[i]); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 2);
    }


    assert(btree_sane(btree1));
    assert(btree_sane(btree2));
    assert(btree_count(btree1) == N/2);
    assert(btree_count(btree2) == N/2);


    for (size_t i = 0; i < N; i+=2) {
        const void *v;
        btree = btree1;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(!v);
    }

    for (size_t i = 0; i < N; i+=2) {
        const void *v;
        btree = btree2;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(!v);
    }


    for (size_t i = 1; i < N; i+=2) {
        const void *v;
        btree = btree1;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 1);
    }

    for (size_t i = 1; i < N; i+=2) {
        const void *v;
        btree = btree2;
        OOM_WAIT( { v = btree_get(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 2);
    }


    for (size_t i = 1; i < N; i += 2) {
        const void *v;
        btree = btree1;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 1);
    }

    for (size_t i = 1; i < N; i += 2) {
        const void *v;
        btree = btree2;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v);
        assert(((struct pair*)v)->val == 2);
    }

    assert(btree_sane(btree1));
    assert(btree_sane(btree2));
    assert(btree_count(btree1) == 0);
    assert(btree_count(btree2) == 0);

    btree_free(btree1);
    btree_free(btree2);
    xfree(pairs);
    assert(!btree_clone(NULL));
}

void test_clone_pairs_diverge(void) {
    test_clone_pairs_diverge_withcallbacks(true);
}

void test_clone_pairs_diverge_nocallbacks(void) {
    test_clone_pairs_diverge_withcallbacks(false);
}


// cloneable object
struct cobj {
    char *key;
    char *val;
};


struct cobj *cobj_new(const char *key, const char *val) {
    struct cobj *obj = xmalloc(sizeof(struct cobj));
    if (!obj) return NULL;
    obj->key = xmalloc(strlen(key)+1);
    if (!obj->key) {
        xfree(obj);
        return NULL;
    }
    obj->val = xmalloc(strlen(val)+1);
    if (!obj->val) {
        xfree(obj->key);
        xfree(obj);
        return NULL;
    }
    strcpy(obj->key, key);
    strcpy(obj->val, val);
    return obj;
}

struct cobj *cobj_clone(struct cobj *obj) {
    if (!obj) return NULL;
    return cobj_new(obj->key, obj->val);
}

void cobj_free(struct cobj *obj) {
    if (!obj) return;
    xfree(obj->key);
    xfree(obj->val);
    xfree(obj);
}

int cobj_compare0(const void *a, const void *b) {
    return strcmp((*(struct cobj**)a)->key, (*(struct cobj**)b)->key);
}

int cobj_compare(const void *a, const void *b, void *udata) {
    assert(udata == nothing);
    return cobj_compare0(a, b);
}

bool bt_cobj_clone(const void *item, void *into, void *udata) {
    (void)udata;
    struct cobj *obj = *(struct cobj**)item;
    struct cobj *obj2 = cobj_clone(obj);
    if (!obj2) return false;
    memcpy(into, &obj2, sizeof(struct cobj*));
    return true;
}

void bt_cobj_free(const void *item, void *udata) {
    (void)udata;
    struct cobj *obj = *(struct cobj**)item;
    cobj_free(obj);
}

struct thctx {
    pthread_mutex_t *mu;
    int nobjs;
    int *ncloned;
    struct btree *btree;
    struct cobj **objs;
};

struct cobj **cobjs_clone_all(struct cobj **objs, int NOBJS) {
    struct cobj **objs2 = xmalloc(NOBJS*sizeof(struct cobj));
    if (!objs2) return NULL;
    memset(objs2, 0, NOBJS*sizeof(struct cobj));
    for (int i = 0; i < NOBJS; i++) {
        objs2[i] = cobj_clone(objs[i]);
        if (!objs2[i]) {
            for (int j = 0; j < i; j++) {
                cobj_free(objs2[i]);
            }
            xfree(objs2);
            return NULL;
        }
    }
    return objs2;
}

void *thdwork(void *tdata) {
    pthread_mutex_t *mu = ((struct thctx *)tdata)->mu;
    int NOBJS = ((struct thctx *)tdata)->nobjs;
    struct cobj **objs = ((struct thctx *)tdata)->objs;
    struct btree *btree = ((struct thctx *)tdata)->btree;
    int *ncloned = ((struct thctx *)tdata)->ncloned;


    // copy the objs and btree
    rsleep(0.1, 0.2);
    struct cobj **objscp = xmalloc(NOBJS*sizeof(struct cobj));
    assert(objscp);
    for (int i = 0; i < NOBJS; i++) {
        objscp[i] = cobj_clone(objs[i]);
    }
    objs = objscp;
    assert(!pthread_mutex_lock(mu));
    struct btree *btreecp = btree_clone(btree);
    assert(btreecp);
    btree = btreecp;
    (*ncloned)++;
    ncloned = NULL;
    assert(!pthread_mutex_unlock(mu));
    rsleep(0.1, 0.2);

    // we now have a clone of the database and the original objects.
    // anything we do to this clone should not affect the original
    shuffle(objs, NOBJS, sizeof(struct cobj*));
    for (int i = 0; i < NOBJS; i++) {
        xfree(objs[i]->val);
        objs[i]->val = rand_key(10);
    }

    // delete every other object
    for (int i = 0; i < NOBJS; i += 2) {
        assert(btree_delete(btree, &objs[i]));

    }
    assert(btree_count(btree) == (size_t)NOBJS/2);




    btree_free(btree);
    for (int i = 0; i < NOBJS; i++) {
        cobj_free(objs[i]);
    }
    xfree(objs);

    return NULL;
}


void test_clone_threads(void) {
    // This should probably be tested with both:
    //
    //   $ run.sh
    //   $ RACE=1 run.sh
    //
    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    int NOBJS = 10000;
    int NCLONES = 20;
    struct cobj **objs = xmalloc(sizeof(struct cobj*)*NOBJS);
    assert(objs);
    struct btree *btree = btree_new_for_test(sizeof(struct cobj*), 5, 
        cobj_compare, nothing);
    assert(btree);
    btree_set_item_callbacks(btree, bt_cobj_clone, bt_cobj_free);
    for (int i = 0; i < NOBJS; i++) {
        char *key = rand_key(10);
        char *val = rand_key(10);
        objs[i] = cobj_new(key, val);
        assert(objs[i]);
        xfree(key);
        xfree(val);
        const void *prev = btree_set(btree, &objs[i]);
        if (prev) {
            // already exists with same key.
            // replace new with old and try again.
            struct cobj *prev_obj = *(struct cobj**)prev;
            btree_set(btree, &prev_obj);
            cobj_free(objs[i]);
            i--;
        }
    }

    assert(btree_count(btree) == (size_t)NOBJS);
    struct btree *btree2 = btree_clone(btree);
    assert(btree2);



    // we now have a list of objects and a btree fill the same objects.
    int ncloned = 0;
    pthread_t *threads = xmalloc(NCLONES*sizeof(pthread_t));
    for (int i = 0; i < NCLONES; i++) {
        assert(!pthread_create(&threads[i], NULL, thdwork, &(struct thctx){
            .mu = &mu,
            .nobjs = NOBJS,
            .objs = objs,
            .btree = btree,
            .ncloned = &ncloned,
        }));
        // assert(!pthread_join(threads[i], NULL));
    }

    // we fired up all the threads, wait for all the cloning to happen.
    while (1) {
        assert(!pthread_mutex_lock(&mu));
        bool ok = ncloned == NCLONES;
        assert(!pthread_mutex_unlock(&mu));
        if (ok) break;
        usleep(10000);
    }
    
    // Now we have NCLONES number of independent btrees
    // Let's delete the original objs array.
    for (int i = 0; i < NOBJS; i++) {
        cobj_free(objs[i]);
    }
    xfree(objs);

    // Let's check if we can still access all of the original objects
    // stored in the btree.
    
    for (int i = 0; i < NOBJS; i++) {
        const void *v = btree_min(btree);
        assert(v);
    }

    btree_free(btree);



    for (int i = 0; i < NCLONES; i++) {
         assert(!pthread_join(threads[i], NULL));
    }
    xfree(threads);


    btree_free(btree2);

    // for (int i = 0; i < NOBJS; i++) {
    //     cobj_free(objs[i]);
    // }
    // xfree(objs);
}


void test_clone_delete_withcallbacks(bool withcallbacks) {
    
    size_t N = 10000;
    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));

    for (size_t i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = 0;
    }
    
    shuffle(pairs, N, sizeof(struct pair));

    struct btree *btree; 
    
    while(!(btree = btree_new_for_test(sizeof(struct pair), 4, 
        compare_pairs, nothing)));
    if (withcallbacks) {
        btree_set_item_callbacks(btree, pair_clone, pair_free);
    }
    for (size_t i = 0; i < N; i++) {
        const void *v;
        OOM_WAIT( { v = btree_load(btree, &pairs[i]); } );
        assert(!v);
    }
    assert(btree_sane(btree));

    struct btree *btree2;
    while(!(btree2 = btree_clone(btree)));

    assert(btree_count(btree) == N);
    assert(btree_sane(btree));
    assert(btree_count(btree2) == N);
    assert(btree_sane(btree2));

    shuffle(pairs, N, sizeof(struct pair));
    for (size_t i = 0; i < N; i++) {
        const void *v;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v);
        assert(btree_sane(btree));
        assert(btree_count(btree) == N-i-1);
    }

    shuffle(pairs, N, sizeof(struct pair));
    for (size_t i = 0; i < N; i++) {
        const void *v;
        struct btree *btree = btree2; // this is needed for OOM_WAIT;
        OOM_WAIT( { v = btree_delete(btree, &pairs[i]); } );
        assert(v);
        assert(btree_sane(btree));
        assert(btree_count(btree) == N-i-1);
    }

    btree_free(btree);
    btree_free(btree2);
    xfree(pairs);
}

void test_clone_delete(void) {
    test_clone_delete_withcallbacks(true);
}

void test_clone_delete_nocallbacks(void) {
    test_clone_delete_withcallbacks(false);
}

void test_clone_pop_withcallbacks(bool withcallbacks) {
    size_t N = 10000;
    struct pair *pairs;
    while (!(pairs = xmalloc(sizeof(struct pair) * N)));

    for (size_t i = 0; i < N; i++) {
        pairs[i].key = i;
        pairs[i].val = 0;
    }
    
    shuffle(pairs, N, sizeof(struct pair));

    struct btree *btree1; 
    
    while(!(btree1 = btree_new_for_test(sizeof(struct pair), 4, 
        compare_pairs, nothing)));
    if (withcallbacks) {
        btree_set_item_callbacks(btree1, pair_clone, pair_free);
    }
    for (size_t i = 0; i < N; i++) {
        const void *v;
        struct btree *btree = btree1;
        OOM_WAIT( { v = btree_load(btree, &pairs[i]); } );
        assert(!v);
    }
    assert(btree_sane(btree1));

    struct btree *btree2;
    while(!(btree2 = btree_clone(btree1)));

    assert(btree_count(btree1) == N);
    assert(btree_sane(btree1));
    assert(btree_count(btree2) == N);
    assert(btree_sane(btree2));

    shuffle(pairs, N, sizeof(struct pair));
    for (size_t i = 0; i < N; i++) {
        const void *v;
        struct btree *btree = btree1;
        OOM_WAIT( { v = btree_pop_min(btree); } );
        assert(v && ((struct pair*)v)->key == (int)i);
        assert(btree_sane(btree));
        assert(btree_count(btree) == N-i-1);
    }

    btree_free(btree1);
    while(!(btree1 = btree_clone(btree2)));
    shuffle(pairs, N, sizeof(struct pair));
    for (size_t i = 0; i < N; i++) {
        const void *v;
        struct btree *btree = btree2; // this is needed for OOM_WAIT;
        OOM_WAIT( { v = btree_pop_max(btree); } );
        assert(v && ((struct pair*)v)->key == (int)(N-i-1));
        assert(btree_sane(btree));
        assert(btree_count(btree) == N-i-1);
    }

    btree_free(btree1);
    btree_free(btree2);
    xfree(pairs);
}

void test_clone_pop(void) {
    test_clone_pop_withcallbacks(true);
}

void test_clone_pop_nocallbacks(void) {
    test_clone_pop_withcallbacks(false);
}



int main(int argc, char **argv) {
    do_chaos_test(test_clone_items);
    do_chaos_test(test_clone_items_nocallbacks);
    do_chaos_test(test_clone_pairs_diverge);
    do_chaos_test(test_clone_pairs_diverge_nocallbacks);
    do_chaos_test(test_clone_delete);
    do_chaos_test(test_clone_delete_nocallbacks);
    do_chaos_test(test_clone_pop);
    do_chaos_test(test_clone_pop_nocallbacks);
    do_test(test_clone_threads);
    return 0;
}
