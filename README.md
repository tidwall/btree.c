# btree.c

A B-tree implementation in C. 

## Features

- Generic interface with support for variable sized items
- Fast sequential bulk loading
- Copy-on-write support
- Supports C99 and up
- Supports custom allocators
- 100% code coverage
- Pretty darn good performance 🚀

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

    printf("\n-- loop iterator (same as previous) --\n");
    struct btree_iter *iter = btree_iter_new(btree);
    bool ok = btree_seek(iter, &(struct user){.first="",.last="Murphy"});
    while (ok) {
        const struct user *user = btree_item(iter);
        printf("%s %s (age=%d)\n", user->first, user->last, user->age);
        ok = btree_next(iter);
    }

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
//
// -- loop iterator (same as previous) --
// Dale Murphy (age=44)
// Jane Murphy (age=47)
```

## Functions

### Basic

```sh
btree_new     # allocate a new btree
btree_free    # free the btree
btree_count   # number of items in the btree
btree_set     # insert or replace an existing item and return the previous
btree_get     # get an existing item
btree_delete  # delete and return an item
btree_clone   # make an clone of the btree using a copy-on-write technique
```

### Callback iteration

```sh
btree_ascend   # iterate over items in ascending order starting at pivot point.
btree_descend  # iterate over items in descending order starting at pivot point.
```

### Loop iteration

```sh
btree_iter_new      # allocate a new the iterator
btree_iter_free     # free the iterator
btree_iter_seek     # seek to an item
btree_iter_next     # iterate to the next item
btree_iter_prev     # iterate to the previous item
btree_iter_first    # seek to the first item in the btree
btree_iter_last     # seek to the last item in the btree
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

*See [btree.h](btree.h) for info on all available functions.

## Testing and benchmarks

```sh
$ tests/run.sh        # run tests
$ tests/run.sh bench  # run benchmarks
```

The following benchmarks were run on my 2021 Apple M1 Max using gcc-12.
The items are simple 4-byte ints.

```
seed=1683231129, max_items=256, count=1000000, item_size=4
load (seq)     1,000,000 ops in 0.010 secs   10.5 ns/op    95,419,847 op/sec  8.32 bytes/op  0.01 allocs/op
load (rand)    1,000,000 ops in 0.175 secs  175.1 ns/op     5,709,457 op/sec  5.92 bytes/op  0.01 allocs/op
set (seq)      1,000,000 ops in 0.080 secs   80.3 ns/op    12,452,524 op/sec  8.32 bytes/op  0.01 allocs/op
set (seq-hint) 1,000,000 ops in 0.025 secs   24.8 ns/op    40,309,577 op/sec  8.32 bytes/op  0.01 allocs/op
set (rand)     1,000,000 ops in 0.165 secs  165.0 ns/op     6,059,210 op/sec  5.93 bytes/op  0.01 allocs/op
get (seq)      1,000,000 ops in 0.081 secs   80.5 ns/op    12,421,588 op/sec
get (seq-hint) 1,000,000 ops in 0.088 secs   87.5 ns/op    11,426,220 op/sec
get (rand)     1,000,000 ops in 0.117 secs  116.9 ns/op     8,555,271 op/sec
delete (rand)  1,000,000 ops in 0.148 secs  147.6 ns/op     6,774,792 op/sec
min            1,000,000 ops in 0.001 secs    1.3 ns/op   797,448,165 op/sec
max            1,000,000 ops in 0.002 secs    1.7 ns/op   579,710,144 op/sec
ascend         1,000,000 ops in 0.001 secs    1.0 ns/op 1,018,329,938 op/sec
descend        1,000,000 ops in 0.001 secs    1.0 ns/op 1,040,582,726 op/sec
pop-min        1,000,000 ops in 0.028 secs   27.7 ns/op    36,039,932 op/sec
pop-max        1,000,000 ops in 0.018 secs   17.7 ns/op    56,404,760 op/sec
```

## License

btree.c source code is available under the MIT License.
