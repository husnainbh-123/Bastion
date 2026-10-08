#!/usr/bin/env bash
# Measure what each search technique is worth: play the full engine against a
# copy with one technique switched off (BASTION_DISABLE) for a fixed number of games.
#
#   tools/testing/ablation.sh ./bastion GAMES feature1 feature2 ...
#
# Feature names: nmp rfp razor lmr lmp futility see histprune singular iir
#                aspiration killers history checkext
set -euo pipefail
engine=$(realpath "$1"); games=$2; shift 2
: "${FASTCHESS:?set FASTCHESS to the fastchess binary}" "${BOOK:?set BOOK to an opening book}"
format=${BOOK##*.}
for feature in "$@"; do
  wrapper=$(mktemp)
  printf '#!/bin/sh\nBASTION_DISABLE=%s exec "%s" "$@"\n' "$feature" "$engine" > "$wrapper"
  chmod +x "$wrapper"
  echo "== without $feature"
  "$FASTCHESS" \
    -engine cmd="$engine" name=full -engine cmd="$wrapper" name="no-$feature" \
    -each tc="${TC:-5+0.05}" option.Hash=16 option.Threads=1 \
    -openings file="$BOOK" format="$format" order=random \
    -rounds $((games / 2)) -repeat -concurrency "${CONCURRENCY:-$(nproc)}" -recover \
    | grep -E "^(Elo|Games|Ptnml)" | tail -3
  rm -f "$wrapper"
done
