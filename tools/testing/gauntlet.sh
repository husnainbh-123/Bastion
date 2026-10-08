#!/usr/bin/env bash
# Estimate Bastion's rating by playing reference engines with known ratings.
#
#   tools/testing/gauntlet.sh ./bastion ROUNDS ref1 ref2 ...
#
# Each reference engine plays ROUNDS game pairs (both colours from the same
# opening). The performance rating against each opponent is its rating plus the
# Elo difference fastchess reports. Uses a balanced opening book (BOOK).
set -euo pipefail
engine=$1; rounds=$2; shift 2
: "${FASTCHESS:?set FASTCHESS to the fastchess binary}" "${BOOK:?set BOOK to an opening book}"
format=${BOOK##*.}
args=()
for ref in "$@"; do args+=(-engine cmd="$ref" name="$(basename "$ref")"); done
"$FASTCHESS" \
  -tournament gauntlet -seeds 1 \
  -engine cmd="$engine" name=bastion "${args[@]}" \
  -each tc="${TC:-8+0.08}" option.Hash=16 \
  -openings file="$BOOK" format="$format" order=random \
  -rounds "$rounds" -repeat -concurrency "${CONCURRENCY:-$(nproc)}" -recover \
  -pgnout file="gauntlet-$(date +%Y%m%d-%H%M%S).pgn"
