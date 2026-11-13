#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Eberon Limited (John Gorman)
# emu_chord.sh TAG NAME ENGINE PRESET FLAGS NOTES [WAV]   (EDIT=1: LFO 1 -> E1 in slot 12, bench_patch.py bank 125)
# boot; 1.40 s flags (bank 126), 1.45 s engine + preset; 1.90 s counters reset (trace starts); 2.00 s NOTES note-ons
# at once (MIDI ch 1, 40, 42, ...); end 3.05 s. Dumps felucca_dbg and bench_st into emu/TAG.raw
TAG=$1 NAME=$2 ENG=$3 PRE=$4 FL=$5 NN=$6 WAV=$7
EMU=${FM1_EMU:-../fm1-emu/target/release/fm1-emu}   # fm1-emu's batch runner
S=${VCOST_DIR:?set VCOST_DIR: the bench work folder (bench/, emu/)}
ELF=$S/bench/$NAME.elf FWSC=$S/bench/$NAME.fwsc
addr() { nm "$ELF" | awk -v s="$1" '$3==s {print $1}'; }
DBG=$(addr felucca_dbg); BST=$(addr bench_st)
hx() { printf '%02x' "$1"; }
K="1380:midi:b0007d,1390:midi:c0$(hx ${EDIT:-0}),1400:midi:b0007e,1410:midi:c0$(hx $FL),1450:midi:b000$(hx $ENG),1460:midi:c0$(hx $PRE)"
K="$K,1900:midi:b0007f,1910:midi:c000"
i=0
while [ $i -lt $NN ]; do K="$K,2000:midi:90$(hx $((40 + 2 * i)))64"; i=$((i + 1)); done
mkdir -p $S/emu/w-$TAG
set -- --fwsc "$FWSC" --ms 3050 --keys "$K" --dump 0x$BST 212
[ -n "$WAV" ] && set -- "$@" --wav "$WAV"
(cd $S/emu/w-$TAG && "$EMU" "$@" > $S/emu/$TAG.raw 2>&1)
