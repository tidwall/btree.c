#!/bin/bash

# ./run.sh [<test-name>]

set -e
cd $(dirname "${BASH_SOURCE[0]}")

finish() { 
    rm -f *.o
    rm -f *.out
    rm -f *.test
    rm -f *.profraw
    rm -fr *.dSYM
    rm -f *.profdata
    echo ;
}
trap finish EXIT

# Use address sanitizer if possible
if [[ "$1" != "bench" ]]; then
    CFLAGS="-O0 -g3 -Wall -Wextra -fstrict-aliasing $CFLAGS"
    if [[ ("$CC" == "" || "$CC" == "clang") && "`which clang`" != "" ]]; then
        CC=clang
        CFLAGS="$CFLAGS -fno-omit-frame-pointer"
        CFLAGS="$CFLAGS -fprofile-instr-generate"
        CFLAGS="$CFLAGS -fcoverage-mapping"
        if [[ "$RACE" == "1" ]]; then
            CFLAGS="$CFLAGS -fsanitize=thread"
        else
            CFLAGS="$CFLAGS -fsanitize=address"
        fi
        CFLAGS="$CFLAGS -fsanitize=undefined"
        CFLAGS="$CFLAGS -fno-inline"
        CFLAGS="$CFLAGS -pedantic"
        WITHCOV="0"
        if [[ "$(which llvm-profdata)" == "" ]]; then
            if [[ "$(which xcrun)" != "" ]]; then
                WITHCOV="1"
                XCRUN=xcrun
            fi
        else
            WITHCOV="1"
        fi
    fi
    CFLAGS=${CFLAGS:-"-O0 -g3 -Wall -Wextra -fstrict-aliasing"}
else
    CFLAGS=${CFLAGS:-"-O3"}
fi
CFLAGS="$CFLAGS -DBTREE_TEST_PRIVATE_FUNCTIONS -DTEST_DEBUG"
CC=${CC:-cc}
echo "CC: $CC"
echo "CFLAGS: $CFLAGS"
$CC --version

if [[ "$1" == "bench" ]]; then
    echo "BENCHMARKING..."
    echo $CC $CFLAGS ../btree.c bench.c
    $CC $CFLAGS ../btree.c bench.c
    ./a.out $@
else
    echo "For benchmarks: 'run.sh bench'"
    if [[ "$RACE" != "1" ]]; then
        echo "For data race check: 'RACE=1 run.sh'"
    fi
    echo "TESTING..."
    for f in *; do 
        if [[ "$f" != test_*.c ]]; then continue; fi 
        if [[ "$1" == test_* ]]; then 
            p=$1
            if [[ "$1" == test_*_* ]]; then
                # fast track matching prefix with two underscores
                suf=${1:5}
                rest=${suf#*_}
                idx=$((${#suf}-${#rest}-${#_}+4))
                p=${1:0:idx}
            fi 
            if [[ "$f" != $p* ]]; then continue; fi
        fi
        $CC $CFLAGS -o $f.test ../btree.c $f
        if [[ "$WITHCOV" == "1" ]]; then
            MallocNanoZone=0 LLVM_PROFILE_FILE="$f.profraw" ./$f.test $@
        else
            ./$f.test $@
        fi
    done
    echo "OK"

    if [[ "$COVREGIONS" == "" ]]; then 
        COVREGIONS="false"
    fi

    if [[ "$WITHCOV" == "1" ]]; then
        $XCRUN llvm-profdata merge *.profraw -o test.profdata
        $XCRUN llvm-cov report *.test ../btree.c -ignore-filename-regex=.test. \
            -j=4 \
            -show-functions=true \
            -instr-profile=test.profdata > /tmp/test.cov.sum.txt
        # echo coverage: $(cat /tmp/test.cov.sum.txt | grep TOTAL | awk '{ print $NF }')
        echo covered: "$(cat /tmp/test.cov.sum.txt | grep TOTAL | awk '{ print $7; }') (lines)"
        $XCRUN llvm-cov show *.test ../btree.c -ignore-filename-regex=.test. \
            -j=4 \
            -show-regions=true \
            -show-expansions=$COVREGIONS \
            -show-line-counts-or-regions=true \
            -instr-profile=test.profdata -format=html > /tmp/test.cov.html
        echo "details: file:///tmp/test.cov.html"
        echo "summary: file:///tmp/test.cov.sum.txt"
    elif [[ "$WITHCOV" == "0" ]]; then
        echo "code coverage not a available"
        echo "install llvm-profdata and use clang for coverage"
    fi

fi
