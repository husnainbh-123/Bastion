#!/usr/bin/env bash
# SPRT between two builds of Bastion using fastchess.
#
#   tools/testing/sprt.sh ./bastion-new ./bastion-old [elo0] [elo1] [tc]
#
# Plays until the test accepts H1 ("new is at least elo1 stronger") or H0 ("new is
# at most elo0 stronger"), with 5% error rates. Requires FASTCHESS (path to the
# fastchess binary) and BOOK (an opening book in EPD or PGN format).
set -euo pipefail
new=$1; old=$2; elo0=${3:-0}; elo1=${4:-5}; tc=${5:-8+0.08}
: "${FASTCHESS:?set FASTCHESS to the fastchess binary}" "${BOOK:?set BOOK to an opening book}"
format=${BOOK##*.}
"$FASTCHESS" \
  -engine cmd="$new" name=new -engine cmd="$old" name=old \
  -each tc="$tc" option.Hash=16 option.Threads=1 \
  -openings file="$BOOK" format="$format" order=random \
  -rounds 20000 -repeat -concurrency "${CONCURRENCY:-$(nproc)}" -recover \
  -sprt elo0="$elo0" elo1="$elo1" alpha=0.05 beta=0.05 model=normalized \
  -pgnout file="sprt-$(date +%Y%m%d-%H%M%S).pgn"
