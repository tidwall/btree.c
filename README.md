# btree.c

A B-tree implementation in C. 

## Features

- Generic interface with support for variable sized items.
- Fast sequential bulk loading
- ANSI C (C99)
- Supports custom allocators
- Pretty darn good performance. 🚀

## Example

```c
#include <stdio.h>
#include <string.h>
#include "btree.h"

struct user {
    char *first;
    char *last;
    int age;
};

int user_compare(const void *a, const void *b, void *udata) {
    const struct user *ua = a;
    const struct user *ub = b;
    int cmp = strcmp(ua->last, ub->last);
    if (cmp == 0) {
        cmp = strcmp(ua->first, ub->first);
    }
    return cmp;
}

bool user_iter(const void *a, void *udata) {
    const struct user *user = a;
    printf("%s %s (age=%d)\n", user->first, user->last, user->age);
    return true;
}

int main() {
    // create a new btree where each item is a `struct user`. 
    struct btree *tr = btree_new(sizeof(struct user), 0, user_compare, NULL);

    // load some users into the btree. Each set operation performas a copy of 
    // the data that is pointed to in the second argument.
    btree_set(tr, &(struct user){ .first="Dale", .last="Murphy", .age=44 });
    btree_set(tr, &(struct user){ .first="Roger", .last="Craig", .age=68 });
    btree_set(tr, &(struct user){ .first="Jane", .last="Murphy", .age=47 });

    struct user *user; 
    
    printf("\n-- get some users --\n");
    user = btree_get(tr, &(struct user){ .first="Jane", .last="Murphy" });
    printf("%s age=%d\n", user->first, user->age);

    user = btree_get(tr, &(struct user){ .first="Roger", .last="Craig" });
    printf("%s age=%d\n", user->first, user->age);

    user = btree_get(tr, &(struct user){ .first="Dale", .last="Murphy" });
    printf("%s age=%d\n", user->first, user->age);

    user = btree_get(tr, &(struct user){ .first="Tom", .last="Buffalo" });
    printf("%s\n", user?"exists":"not exists");


    printf("\n-- iterate over all users --\n");
    btree_ascend(tr, NULL, user_iter, NULL);

    printf("\n-- iterate beginning with last name `Murphy` --\n");
    btree_ascend(tr, &(struct user){.first="",.last="Murphy"}, user_iter, NULL);

    btree_free(tr);
}

// output:
// -- get some users --
// Jane age=47
// Roger age=68
// Dale age=44
// not exists
// 
// -- iterate over all users --
// Roger Craig (age=68)
// Dale Murphy (age=44)
// Jane Murphy (age=47)
// 
// -- iterate beginning with last name `Murphy` --
// Dale Murphy (age=44)
// Jane Murphy (age=47)
```

## Functions

### Basic

```sh
btree_new      # allocate a new btree
btree_free     # free the btree
btree_count    # number of items in the btree
btree_set      # insert or replace an existing item and return the previous
btree_get      # get an existing item
btree_delete   # delete and return an item
```

### Iteration

```sh
btree_ascend   # iterate over items in ascending order starting at pivot point.
btree_descend  # iterate over items in descending order starting at pivot point.
```

### Queues

```sh
btree_pop_min  # remove and return the first item in the btree
btree_pop_max  # remove and return the last item in the btree
btree_min      # return the first item in the btree
btree_max      # return the last item in the btree
```

### Bulk loading

```sh
btree_load     # same as btree_set but optimized for fast loading, 10x boost.
```

## Testing and benchmarks

```sh
$ cc -DBTREE_TEST btree.c && ./a.out              # run tests
$ cc -DBTREE_TEST -O3 btree.c && BENCH=1 ./a.out  # run benchmarks
```

The following benchmarks were run on my 2019 Macbook Pro (2.4 GHz 8-Core Intel Core i9) using gcc-9.
The items are simple 4-byte ints.

```
load (seq)     1000000 ops in 0.010 secs, 10 ns/op, 98000784 op/sec, 6.92 bytes/op, 0.01 allocs/op
set (seq)      1000000 ops in 0.069 secs, 69 ns/op, 14459434 op/sec, 8.29 bytes/op, 0.01 allocs/op
get (seq)      1000000 ops in 0.065 secs, 65 ns/op, 15369010 op/sec
load (rand)    1000000 ops in 0.212 secs, 212 ns/op, 4718740 op/sec, 5.94 bytes/op, 0.01 allocs/op
set (rand)     1000000 ops in 0.175 secs, 175 ns/op, 5726065 op/sec, 5.88 bytes/op, 0.01 allocs/op
get (rand)     1000000 ops in 0.163 secs, 163 ns/op, 6125687 op/sec
delete (rand)  1000000 ops in 0.177 secs, 177 ns/op, 5644647 op/sec
set (seq-hint) 1000000 ops in 0.034 secs, 34 ns/op, 29140076 op/sec, 8.29 bytes/op, 0.01 allocs/op
get (seq-hint) 1000000 ops in 0.053 secs, 53 ns/op, 18774054 op/sec
min            1000000 ops in 0.001 secs, 1 ns/op, 709723208 op/sec
max            1000000 ops in 0.002 secs, 2 ns/op, 611995104 op/sec
pop-min        1000000 ops in 0.035 secs, 35 ns/op, 28408284 op/sec
pop-max        1000000 ops in 0.023 secs, 23 ns/op, 42607584 op/sec
```

## License

btree.c source code is available under the MIT License.
