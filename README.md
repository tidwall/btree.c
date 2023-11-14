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
    bool ok = btree_iter_seek(iter, &(struct user){.first="",.last="Murphy"});
    while (ok) {
        const struct user *user = btree_iter_item(iter);
        printf("%s %s (age=%d)\n", user->first, user->last, user->age);
        ok = btree_iter_next(iter);
    }
    btree_iter_free(iter);

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

The following benchmarks were run on my 2021 Apple M1 Max using clang-17.
The items are simple 4-byte ints.

```
seed=1699992514, max_items=32, count=1000000, item_size=4
load (seq)     1,000,000 ops in 0.019 secs   19.3 ns/op    51,939,957 op/sec 10.67 bytes/op  0.07 allocs/op
load (rand)    1,000,000 ops in 0.106 secs  105.7 ns/op     9,461,185 op/sec  7.28 bytes/op  0.05 allocs/op
set (seq)      1,000,000 ops in 0.078 secs   78.0 ns/op    12,817,719 op/sec 10.67 bytes/op  0.07 allocs/op
set (seq-hint) 1,000,000 ops in 0.039 secs   39.3 ns/op    25,415,544 op/sec 10.67 bytes/op  0.07 allocs/op
set (rand)     1,000,000 ops in 0.105 secs  105.0 ns/op     9,524,172 op/sec  7.26 bytes/op  0.05 allocs/op
get (seq)      1,000,000 ops in 0.042 secs   41.8 ns/op    23,930,887 op/sec
get (seq-hint) 1,000,000 ops in 0.046 secs   46.5 ns/op    21,525,744 op/sec
get (rand)     1,000,000 ops in 0.093 secs   93.1 ns/op    10,745,524 op/sec
delete (rand)  1,000,000 ops in 0.120 secs  120.4 ns/op     8,304,958 op/sec
min            1,000,000 ops in 0.003 secs    3.1 ns/op   322,372,662 op/sec
max            1,000,000 ops in 0.003 secs    3.2 ns/op   311,429,461 op/sec
ascend         1,000,000 ops in 0.001 secs    1.4 ns/op   706,713,780 op/sec
descend        1,000,000 ops in 0.001 secs    1.2 ns/op   801,282,051 op/sec
pop-min        1,000,000 ops in 0.023 secs   23.2 ns/op    43,044,077 op/sec
pop-max        1,000,000 ops in 0.020 secs   19.9 ns/op    50,130,338 op/sec
```

## License

btree.c source code is available under the MIT License.
