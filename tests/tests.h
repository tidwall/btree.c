#ifndef TESTS_H
#define TESTS_H

#include <unistd.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <ctype.h>
#include "../btree.h"

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wcompound-token-split-by-macro"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wpedantic"
#endif


// private btree functions
bool btree_sane(const struct btree *btree);
int btree_compare(const struct btree *btree, const void *a, const void *b);
void btree_print(struct btree *btree, void (*print)(void *item));

int64_t crand(void) {
    uint64_t seed = 0;
    FILE *urandom = fopen("/dev/urandom", "r");
    assert(urandom);
    assert(fread(&seed, sizeof(uint64_t), 1, urandom));
    fclose(urandom);
    return (int64_t)(seed>>1);
}

void seedrand(void) {
    srand(crand());
}

static int64_t seed = 0;

#define do_test0(name, trand) { \
    if (argc < 2 || strstr(#name, argv[1])) { \
        if ((trand)) { \
            seed = getenv("SEED")?atoi(getenv("SEED")):crand(); \
            printf("SEED=%lld\n", seed); \
            srand(seed); \
        } else { \
            seedrand(); \
        } \
        printf("%s\n", #name); \
        init_test_allocator(false); \
        name(); \
        cleanup(); \
        cleanup_test_allocator(); \
    } \
}

#define do_test(name) do_test0(name, 0)
#define do_test_rand(name) do_test0(name, 1)

#define do_chaos_test(name) { \
    if (argc < 2 || strstr(#name, argv[1])) { \
        printf("%s\n", #name); \
        seedrand(); \
        init_test_allocator(true); \
        name(); \
        cleanup(); \
        cleanup_test_allocator(); \
    } \
}

static void shuffle(void *array, size_t numels, size_t elsize) {
    if (numels < 2) return;
    char tmp[elsize];
    char *arr = array;
    for (size_t i = 0; i < numels - 1; i++) {
        int j = i + rand() / (RAND_MAX / (numels - i) + 1);
        memcpy(tmp, arr + j * elsize, elsize);
        memcpy(arr + j * elsize, arr + i * elsize, elsize);
        memcpy(arr + i * elsize, tmp, elsize);
    }
}

void cleanup(void) {
}

atomic_int total_allocs = 0;
atomic_int total_mem = 0;

double now(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec*1e9 + now.tv_nsec) / 1e9;
}

static bool rand_alloc_fail = false;
// 1 in 10 chance malloc or realloc will fail.
static int rand_alloc_fail_odds = 3; 

static void *xmalloc(size_t size) {
    if (rand_alloc_fail && rand()%rand_alloc_fail_odds == 0) {
        return NULL;
    }
    void *mem = malloc(sizeof(uint64_t)+size);
    assert(mem);
    *(uint64_t*)mem = size;
    atomic_fetch_add(&total_allocs, 1);
    atomic_fetch_add(&total_mem, (int)size);
    return (char*)mem+sizeof(uint64_t);
}

static void xfree(void *ptr) {
    if (ptr) {
        atomic_fetch_sub(&total_mem,
            (int)(*(uint64_t*)((char*)ptr-sizeof(uint64_t))));
        atomic_fetch_sub(&total_allocs, 1);
        free((char*)ptr-sizeof(uint64_t));
    }
}

static void *(*__malloc)(size_t) = NULL;
static void (*__free)(void *) = NULL;

void init_test_allocator(bool random_failures) {
    rand_alloc_fail = random_failures;
    __malloc = xmalloc;
    __free = xfree;
}

void cleanup_test_allocator(void) {
    if (atomic_load(&total_allocs) > 0 || atomic_load(&total_mem) > 0) {
        fprintf(stderr, "test failed: %d unfreed allocations, %d bytes\n",
            atomic_load(&total_allocs), atomic_load(&total_mem));
        exit(1);
    }
    __malloc = NULL;
    __free = NULL;
}

// struct btree *btree_new_for_test(size_t elsize, size_t max_items,
//     int (*compare)(const void *a, const void *b, void *udata),
//     void *udata)
// {
//     return btree_new_with_allocator(__malloc, NULL, __free, elsize, max_items, 
//         compare, udata);
// }

struct btree *btree_new_for_test(size_t elsize, size_t degree,
    int (*compare)(const void *a, const void *b, void *udata),
    void *udata)
{
    return btree_new_with_allocator(__malloc, NULL, __free, elsize, 
        degree, compare, udata);
}


char *commaize(unsigned int n) {
    char s1[64];
    char *s2 = malloc(64);
    assert(s2);
    memset(s2, 0, sizeof(64));
    snprintf(s1, sizeof(s1), "%d", n);
    int i = strlen(s1)-1; 
    int j = 0;
	while (i >= 0) {
		if (j%3 == 0 && j != 0) {
            memmove(s2+1, s2, strlen(s2)+1);
            s2[0] = ',';
		}
        memmove(s2+1, s2, strlen(s2)+1);
		s2[0] = s1[i];
        i--;
        j++;
	}
	return s2;
}

#define bench(name, N, code) {{ \
    if (strlen(name) > 0) { \
        printf("%-14s ", name); \
    } \
    size_t tmem = (size_t)atomic_load(&total_mem); \
    size_t tallocs = (size_t)atomic_load(&total_allocs); \
    uint64_t bytes = 0; \
    clock_t begin = clock(); \
    for (int i = 0; i < N; i++) { \
        (code); \
    } \
    clock_t end = clock(); \
    double elapsed_secs = (double)(end - begin) / CLOCKS_PER_SEC; \
    double bytes_sec = (double)bytes/elapsed_secs; \
    double ns_op = elapsed_secs/(double)N*1e9; \
    char *pops = commaize(N); \
    char *psec = commaize((double)N/elapsed_secs); \
    printf("%s ops in %.3f secs %6.1f ns/op %13s op/sec", \
        pops, elapsed_secs, ns_op, psec); \
    free(psec); \
    free(pops); \
    if (bytes > 0) { \
        printf(" %.1f GB/sec", bytes_sec/1024/1024/1024); \
    } \
    if ((size_t)atomic_load(&total_mem) > tmem) { \
        size_t used_mem = (size_t)atomic_load(&total_mem)-tmem; \
        printf(" %5.2f bytes/op", (double)used_mem/N); \
    } \
    if ((size_t)atomic_load(&total_allocs) > tallocs) { \
        size_t used_allocs = (size_t)atomic_load(&total_allocs)-tallocs; \
        printf(" %5.2f allocs/op", (double)used_allocs/N); \
    } \
    printf("\n"); \
}}


#define DEF_MAX_ITEMS 6
#define DEF_DEGREE    3
#define DEF_N         2000

#define OOM_WAIT(run) do { run ; } while (btree_oom(btree))
#define OOM_WAIT(run) do { run ; } while (btree_oom(btree))

char nothing[] = "nothing";

static int compare_ints0(const void *a, const void *b) {
    int ia = *(int*)a;
    int ib = *(int*)b;
    return (ia < ib) ? -1 : (ia > ib);
}

int compare_ints(const void *a, const void *b, void *udata) {
    assert(udata == nothing);
    return compare_ints0(a, b);
}

int compare_ints1(const void *a, const void *b, void *udata) {
    (void)udata;
    return compare_ints0(a, b);
}


struct iter_ctx {
    bool rev;
    struct btree *btree;
    const void *last;
    int count;
    bool bad;
    int stop_at;
};

bool iter(const void *item, void *udata) {
    struct iter_ctx *ctx = udata;
    if (ctx->stop_at > 0 && ctx->count == ctx->stop_at) {
        return false;
    }
    if (ctx->bad) {
        return false;
    }
    if (ctx->last) {
        if (ctx->rev) {
            if (btree_compare(ctx->btree, item, ctx->last) >= 0) {
                ctx->bad = true;
                return false;
            }
        } else {
            if (btree_compare(ctx->btree, ctx->last, item) >= 0) {
                ctx->bad = true;
                return false;
            }
        }
    }
    ctx->last = item;
    ctx->count++;
    return true;
}


struct pair {
    int key;
    int val;
};

int compare_pairs_nudata(const void *a, const void *b) {
    return ((struct pair*)a)->key - ((struct pair*)b)->key;
}

int compare_pairs(const void *a, const void *b, void *udata) {
    assert(udata == nothing);
    return ((struct pair*)a)->key - ((struct pair*)b)->key;
}

struct pair_keep_ctx {
    struct pair last;
    int count;
};


bool pair_update_check(const void *item, void *udata) {
    int half = *(int*)udata;
    struct pair *pair = (struct pair *)item;
    if (pair->key < half) {
        assert(pair->val == pair->key + 1);
    } else {
        assert(pair->val == pair->key + 2);
    }
    return true;
}

bool pair_update_check_desc(const void *item, void *udata) {
    int half = *(int*)udata;
    struct pair *pair = (struct pair *)item;
    if (pair->key > half) {
        assert(pair->val == pair->key + 1);
    } else {
        assert(pair->val == pair->key + 2);
    }
    return true;
}

char *rand_key(int nchars) {
    char *key = xmalloc(nchars+1);
    for (int i = 0 ; i < nchars; i++) {
        key[i] = (rand()%26)+'a';
    }
    key[nchars] = '\0';
    return key;
}

// rsleep randomly sleeps between min_secs and max_secs
void rsleep(double min_secs, double max_secs) {
    double duration = max_secs - min_secs;
    double start = now();
    while (now()-start < duration) {
        usleep(10000); // sleep for ten milliseconds
    }
}

#endif // TESTS_H


