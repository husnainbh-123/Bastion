# Testing and results

Everything in this file was measured on the project's development machine (a
2-core cloud VM) with the scripts in [`tools/testing`](../tools/testing) and
[fastchess](https://github.com/Disservin/fastchess). Raw numbers are given with
their uncertainty: chess results are noisy, and a few hundred games still leave
an error margin of tens of Elo.

## Correctness

| Test | What it checks | Run it |
| --- | --- | --- |
| Perft suite | Legal move generation in 21 positions on every build, 6 deeper ones on demand (761,234,712 leaf nodes), including castling, en passant, promotion and check edge cases | `./build/perft_tests` and `./build/perft_tests --deep` |
| Unit tests | Incremental Zobrist keys equal keys computed from scratch; `gives_check` matches the real result of every move; all 65,536 16-bit moves are accepted by `pseudo_legal` exactly when legal; SEE; repetition, 50-move and material draws; malformed FEN rejected | `./build/unit_tests` |
| Sanitizers | The whole suite under AddressSanitizer and UndefinedBehaviorSanitizer | `cmake -DBASTION_SANITIZE=ON` |
| Fuzzing | libFuzzer on the FEN parser and the UCI command handler; about a million inputs per minute each, no crashes | `cmake -DBASTION_FUZZ=ON` |
| Bench | Fixed-depth search of 36 positions; the node count is a fingerprint of the search and is checked against the commit message in CI | `./build/bastion bench` |
| WebAssembly | The browser build passes rules and search checks under Node | `node tests/wasm_test.mjs` |

## Playing strength

Bastion's rating was estimated by playing reference engines whose ratings are known on the
[CCRL blitz list](https://computerchess.org.uk/ccrl/404/) scale:

- **Stash 25** ([mhouppin/stash-bot](https://github.com/mhouppin/stash-bot)), rated 2744 in the data Stockfish's
  developers used to calibrate Stockfish's skill levels
  ([Stockfish commit a08b8d4](https://github.com/official-stockfish/Stockfish/commit/a08b8d4e9711c20acedbfe17d618c3c384b339ec)),
  which places Stash versions on the CCRL scale.
- **Stockfish at fixed skill levels**, using the exact Stockfish version from that commit (dev-20230122-a08b8d4e,
  network nn-1e7ca356472e) so the ratings published for each level apply.

That calibration states its Stash ratings are only accurate to about ±100 Elo, which is larger than the statistical
error below. Conditions: 8 seconds plus 0.08 seconds per move for each side, one thread and 16 MB hash per engine, balanced openings from
`8moves_v3.pgn` with each opening played once with each colour, two games at a time on a 2-core VM. Stash ran with
the same 30 ms move overhead as Bastion: its default of 100 ms is a real handicap at this time control, so games
played before that was noticed are not counted.

| Opponent | Rating | Games | Wins | Draws | Losses | Bastion scored | Performance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Stash 25 | 2744 | 60 | 43 | 10 | 7 | 80.0% | 2985 |
| Stockfish, skill level 11 | 2856 | 30 | 3 | 10 | 17 | 26.7% | 2680 |
| Stockfish, skill level 13 | 2973 | 44 | 10 | 7 | 27 | 30.7% | 2831 |
| Stockfish, skill level 15 | 3070 | 14 | 4 | 3 | 7 | 39.3% | 2994 |
| Stockfish, skill level 17 | 3141 | 12 | 1 | 1 | 10 | 12.5% | 2803 |
| **All games** | | **160** | | | | | **2867** (2817–2916) |

The estimate is the maximum-likelihood rating over all games (`tools/testing/elo_estimate.py`), with a 95%
interval from bootstrapping the games.

**The two kinds of reference disagree.** Against Stash 25 alone Bastion performed at about 2985 (2898–3096, 60 games); against the Stockfish levels alone, at about 2801 (2724–2867, 100 games). A likely reason is how Stockfish's skill levels work: level *n* picks its move, with some deliberate randomness, from a search to depth *n* + 1 ([`Skill::time_to_pick`](https://github.com/official-stockfish/Stockfish/blob/a08b8d4e9711c20acedbfe17d618c3c384b339ec/src/search.cpp#L107)), so it loses little strength at a fast time control. Stash and Bastion search for as long as the clock allows, so both play below their usual strength at 8 seconds a game.

A direct check points the same way, though 40 games are not enough to be conclusive: at the same time control, Stockfish level 11 scored **75%** against Stash 25 (27 wins, 6 draws and 7 losses for Stockfish), where their published ratings (2856 and 2744) predict 66%. If the Stockfish levels are relatively stronger at fast time controls, Bastion's results against them understate its rating, and the Stash-based figure is closer to its rating at the time control the CCRL list uses. The combined estimate above counts every game and sits between the two.

## Development history

| Version | Commit | Change | Measured |
| --- | --- | --- | --- |
| Before tuning | [`d523983`](https://github.com/husnainbh-123/Bastion/commit/d523983) | Complete search; evaluation weights from the PeSTO piece-square tables, every other term zero | baseline |
| 1.0 | [`20d1fe3`](https://github.com/husnainbh-123/Bastion/commit/20d1fe3) | All 564 evaluation weight pairs tuned on self-play data | **+237 ± 51 Elo** against the version before (140 games at 5+0.05: +98 =27 −15, 79.6%) |

Gains measured between two versions of the same engine are usually larger than the same change is worth
against other engines, so tuning's real effect on the rating is probably smaller than +237.

## What each search technique is worth

Each technique was switched off with `BASTION_DISABLE` and the result played against the full engine:
200 games per technique at a fixed 20,000 nodes per move, so the comparison measures how well the
search uses its nodes rather than raw speed, with balanced openings played with both colours. The number is
the Elo the engine loses without the technique, with its 95% error margin.

| Technique | Elo without it | Games (W–D–L for the full engine) |
| --- | ---: | ---: |
| History move ordering | -238 ± 51 | 200 (141–37–22) |
| Late move reductions | -151 ± 46 | 200 (116–50–34) |
| Reverse futility pruning | -81 ± 38 | 200 (90–66–44) |
| Singular extensions | -56 ± 39 | 200 (85–62–53) |
| Late move pruning | -45 ± 38 | 200 (81–64–55) |
| Null move pruning | -26 ± 37 | 200 (76–63–61) |
| Futility pruning | -7 ± 36 | 200 (60–84–56) |

A few hundred games can only measure large effects: a margin of ±40 Elo means the smaller results here are
consistent with anything from no effect to a clear gain. Fixed-node tests also understate techniques whose
benefit grows with search depth, because 20,000 nodes is a shallow search.

## Evaluation tuning

| | |
| --- | --- |
| Self-play games | 11,978 from random 8–9 ply openings; unbalanced openings (\|eval\| > 300 cp at depth 8) rejected |
| Search per move | 5,000 nodes |
| Positions kept | 1,024,029 quiet positions (side to move not in check, best move not a capture or promotion) |
| Results | 36.0% white wins, 27.0% draws, 37.0% black wins (share of positions) |
| Weights | 564 middlegame/endgame pairs (1,128 numbers), starting from the PeSTO tables and zeros |
| Fit | K = 1.3415, 2,000 epochs of Adam; mean squared error 0.101634 → 0.095206 |
| Effect in games | +237 ± 51 Elo against the untuned version (140 games) |

The tuner treats the evaluation as a linear function of its weights: for every position it records how often
each weight is used by each side (`Eval::evaluate_trace`), checks that this linear model reproduces the real
evaluation exactly (0 mismatches over all positions), and then fits all weights at once.

## Reproducing

```bash
export FASTCHESS=/path/to/fastchess BOOK=/path/to/8moves_v3.pgn
tools/testing/gauntlet.sh ./build/bastion 30 ./stash-v25                    # rating estimate (60 games)
tools/testing/sprt.sh ./bastion-new ./bastion-old 0 5                       # does a change gain Elo?
NODES=20000 tools/testing/ablation.sh ./build/bastion 200 nmp lmr           # value of individual techniques
python3 tools/testing/elo_estimate.py results.json                          # combine gauntlet results

# Tuning data: two processes, 7,000 games each (games from unbalanced openings are skipped)
./build/bastion datagen 7000 5000 selfplay-1.txt 7919 &
./build/bastion datagen 7000 5000 selfplay-2.txt 15838 &
wait; cat selfplay-1.txt selfplay-2.txt > selfplay.txt
./build/tuner selfplay.txt 50000000 2000 2 > src/eval_params.h              # fit the evaluation
```
