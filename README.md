# btree.c

A B-tree implementation in C. 

## Features

- Generic interface with support for variable sized items.
- Fast sequential bulk loading
- ANSI C (C17)
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
$ tests/run.sh        # run tests
$ tests/run.sh bench  # run benchmarks
```

The following benchmarks were run on my 2021 Apple M1 Max using gcc-12.
The items are simple 4-byte ints.

```
seed=1682714329, max_items=256, count=1000000, item_size=4
load (seq)     1000000 ops in 0.007 secs    7.3 ns/op   137,287,204 op/sec  6.93 bytes/op  0.01 allocs/op
load (rand)    1000000 ops in 0.166 secs  166.0 ns/op     6,025,475 op/sec  5.89 bytes/op  0.01 allocs/op
set (seq)      1000000 ops in 0.040 secs   40.0 ns/op    25,012,506 op/sec  8.30 bytes/op  0.01 allocs/op
set (seq-hint) 1000000 ops in 0.022 secs   22.2 ns/op    45,108,033 op/sec  8.30 bytes/op  0.01 allocs/op
set (rand)     1000000 ops in 0.157 secs  157.4 ns/op     6,354,168 op/sec  5.97 bytes/op  0.01 allocs/op
get (seq)      1000000 ops in 0.053 secs   52.5 ns/op    19,044,354 op/sec
get (seq-hint) 1000000 ops in 0.036 secs   35.7 ns/op    27,984,552 op/sec
get (rand)     1000000 ops in 0.154 secs  153.7 ns/op     6,508,001 op/sec
delete (rand)  1000000 ops in 0.154 secs  154.0 ns/op     6,492,705 op/sec
min            1000000 ops in 0.001 secs    1.2 ns/op   805,152,979 op/sec
max            1000000 ops in 0.002 secs    1.7 ns/op   582,750,582 op/sec
ascend         1000000 ops in 0.001 secs    1.0 ns/op 1,010,101,010 op/sec
descend        1000000 ops in 0.001 secs    1.0 ns/op 1,024,590,163 op/sec
pop-min        1000000 ops in 0.030 secs   29.6 ns/op    33,767,812 op/sec
pop-max        1000000 ops in 0.022 secs   21.8 ns/op    45,827,413 op/sec
asc-del-odds   1000000 ops in 0.015 secs   14.6 ns/op    68,436,901 op/sec
desc-del-odds  1000000 ops in 0.017 secs   17.0 ns/op    58,775,126 op/sec
```

## License

btree.c source code is available under the MIT License.
