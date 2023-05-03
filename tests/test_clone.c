#include <pthread.h>
#include "tests.h"

void pair_print(void *item) {
    printf("%d:%d", ((struct pair*)item)->key, ((struct pair*)item)->val);
}

void test_clone_various(void) {

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
    while(!(btree1 = btree_new_for_test(sizeof(struct pair), 32, 
        compare_pairs, nothing)));

    for (size_t i = 0; i < N; i++) {
        const void *prev;
        btree = btree1;
        OOM_WAIT( { prev = btree_set(btree1, &pairs[i]); } );
        assert(!prev);
    }
    assert(btree_count(btree1) == N);

    struct btree *btree2;
    while (!(btree2 = btree_clone(btree1)));

    for (size_t i = 0; i < N; i++) {
        struct pair pair2 = pairs[i];
        pair2.val++;
        const void *prev;
        btree = btree2;
        OOM_WAIT( { prev = btree_set(btree2, &pair2); } );
        assert(prev);
        assert(((struct pair*)prev)->val == 0);
    }



    for (size_t i = 0; i < N; i++) {
        struct pair pair1;
        struct pair pair2;


        btree = btree1;
        OOM_WAIT( { pair1 = *(struct pair*)btree_set(btree1, &pairs[i]); } );
        btree = btree2;
        OOM_WAIT( { pair2 = *(struct pair*)btree_set(btree2, &pairs[i]); } );


        assert(pair1.key == pair2.key);
        assert(pair1.val == pair2.val-1);
        
    }

    btree_free(btree1);
    btree_free(btree2);
    xfree(pairs);
    assert(!btree_clone(NULL));
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

void cobj_free(struct cobj *obj) {
    if (!obj) return;
    xfree(obj->key);
    xfree(obj->val);
    xfree(obj);
}

int cobj_compare(const void *a, const void *b, void *udata) {
    assert(!udata);
    return strcmp((*(struct cobj**)a)->key, (*(struct cobj**)b)->key);
}

bool bt_cobj_clone(const void *item, void *into, void *udata) {
    (void)udata;
    struct cobj *obj = *(struct cobj**)item;
    struct cobj *obj2 = cobj_new(obj->key, obj->val);
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
    struct cobj **objs;
    struct btree *btree;
};

void *thdwork(void *tdata) {
    pthread_mutex_t *mu = ((struct thctx *)tdata)->mu;
    // int NOBJS = ((struct thctx *)tdata)->nobjs;
    // struct cobj **objs = ((struct thctx *)tdata)->objs;
    struct btree *btree = ((struct thctx *)tdata)->btree;

    rsleep(0.1, 0.2);
    assert(!pthread_mutex_lock(mu));
    struct btree *btree2 = btree_clone(btree);
    assert(!pthread_mutex_unlock(mu));
    rsleep(0.1, 0.2);

    // we now have a clone of the database




    btree_free(btree2);
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
    int NCLONES = 100;
    struct cobj **objs = xmalloc(sizeof(struct cobj*)*NOBJS);
    struct btree *btree = btree_new_for_test(sizeof(struct cobj*), 12, 
        cobj_compare, NULL);
    btree_set_item_callbacks(btree, bt_cobj_clone, bt_cobj_free);
    for (int i = 0; i < NOBJS; i++) {
        char *key = rand_key(10);
        char *val = rand_key(10);
        objs[i] = cobj_new(key, val);
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

    // assert(btree_count(btree) == (size_t)NOBJS);

    // we now have a list of objects and a btree fill the same objects.
    pthread_t *threads = xmalloc(NCLONES*sizeof(pthread_t));
    for (int i = 0; i < NCLONES; i++) {
        assert(!pthread_create(&threads[i], NULL, thdwork, &(struct thctx){
            .mu = &mu,
            .nobjs = NOBJS,
            .objs = objs,
            .btree = btree,
        }));
        // assert(!pthread_join(threads[i], NULL));
    }
    for (int i = 0; i < NCLONES; i++) {
         assert(!pthread_join(threads[i], NULL));
    }

    xfree(threads);





    btree_free(btree);
    for (int i = 0; i < NOBJS; i++) {
        cobj_free(objs[i]);
    }
    xfree(objs);
}

int main(int argc, char **argv) {
    do_chaos_test(test_clone_various);
    do_test(test_clone_threads);
    return 0;
}
