#include "tests.h"


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

void test_clone_items(void) {
    int N = 10000;
    struct cobj *objs = xmalloc(sizeof(struct cobj*)*N);
    xfree(objs);
}

int main(int argc, char **argv) {
    do_chaos_test(test_clone_various);
    do_test(test_clone_items);
    return 0;
}
