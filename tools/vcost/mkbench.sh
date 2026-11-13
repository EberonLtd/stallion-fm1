#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
# The voice cost bench (tools/gen_vcost.py's data, tools/vcost_bench.tsv), on fm1-emu; scratch builds, never flashed:
#   VCOST_DIR=/some/scratch sh tools/vcost/mkbench.sh cost16 . NVOICE=16          the bench build (shedding off)
#   VCOST_DIR=... sh tools/vcost/emu_chord.sh C-ANALOG-16 cost16 0 27 32 16       one run (see emu_chord.sh)
#   VCOST_DIR=... python3 tools/vcost/summarise.py tsv C- F > rows.tsv            the rows; a before / after table:
#   VCOST_DIR=... python3 tools/vcost/summarise.py table TAG...
# mkbench.sh NAME SRC_TREE [env...]: copy the tree (committed + working files), patch it with bench_patch.py, build
# into bench/NAME.{fwsc,elf}. Extra env (NVOICE=16 BENCH_SHED=1 BOARD=...) passed through.
set -e
S=${VCOST_DIR:?set VCOST_DIR: the bench work folder}
HERE=$(cd "$(dirname "$0")" && pwd)
PY=${PYTHON:-python3}
NAME=$1 SRC=$2
shift 2
D=$S/bench/t-$NAME-$(date +%s)
mkdir -p $D
if git -C $SRC rev-parse --git-dir >/dev/null 2>&1; then
    (cd $SRC && git ls-files -co --exclude-standard | grep -v '^build/' | tar -cf - -T -) | tar -xf - -C $D
else                                                    # (an exported tree: git archive REV | tar -x)
    (cd $SRC && tar -cf - --exclude ./build .) | tar -xf - -C $D
fi
mkdir -p $D/build && cp -r $SRC/build/gen $D/build/
"$PY" "$HERE/bench_patch.py" $D
cd $D && env PYTHON="$PY" "$@" sh build.sh > $S/bench/b-$NAME.log 2>&1 || { tail -20 $S/bench/b-$NAME.log; exit 1; }
cp build/felucca.fwsc $S/bench/$NAME.fwsc 2>/dev/null || cp build/stallion-smk37.fwsc $S/bench/$NAME.fwsc
cp build/felucca.elf $S/bench/$NAME.elf
grep "ok    image" $S/bench/b-$NAME.log
