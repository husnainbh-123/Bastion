# How Bastion works

This is a guided tour of the engine, written so that someone who has read it can
explain any part of the code: what it does, why it is built that way, and how we
know it works. Each section points at the files involved.

- [1. The big picture](#1-the-big-picture)
- [2. Representing the board](#2-representing-the-board)
- [3. Generating moves](#3-generating-moves)
- [4. Searching](#4-searching)
- [5. Evaluating positions](#5-evaluating-positions)
- [6. Tuning the evaluation](#6-tuning-the-evaluation)
- [7. Testing](#7-testing)
- [8. The website and WebAssembly](#8-the-website-and-webassembly)
- [9. Playing on Lichess](#9-playing-on-lichess)
- [10. Questions you should be able to answer](#10-questions-you-should-be-able-to-answer)

---

## 1. The big picture

A chess engine is a program that, given a position, picks a move. Bastion does
it the classical way:

1. **Represent** the position compactly (`position.h`, `bitboard.h`).
2. **Generate** every legal move (`movegen.cpp`).
3. **Search** the tree of possible continuations as deeply as time allows
   (`search.cpp`), trying promising moves first (`movepick.cpp`) and remembering
   results in a hash table (`tt.cpp`).
4. **Evaluate** the positions at the edge of that tree with a scoring function
   (`evaluate.cpp`), whose weights were learned from self-play games
   (`datagen.cpp`, `tools/tuner`).

It talks to the outside world through UCI, the Universal Chess Interface
(`uci.cpp`): a line-based text protocol that every chess GUI, testing tool and
the Lichess bridge understands. `position startpos moves e2e4` sets up a
position; `go wtime 60000 btime 60000` starts a search; the engine answers
`bestmove e7e5`.

| File | Responsibility |
| --- | --- |
| `types.h` | Squares, pieces, the 16-bit `Move`, packed middlegame/endgame scores |
| `bitboard.*` | Precomputed attack tables and magic bitboards |
| `position.*` | The board, FEN parsing, make/unmake, check/pin detection, SEE |
| `movegen.*` | Pseudo-legal and legal move generation |
| `evaluate.*`, `eval_params.h` | Static evaluation and its tuned weights |
| `tt.*` | Transposition table |
| `movepick.*` | Staged move ordering |
| `search.*` | Iterative deepening, alpha-beta, pruning, threads, time management |
| `uci.*` | Protocol, options, `bench`, `perft` and debugging commands |
| `notation.*` | SAN move names and game-over detection (used by the website) |
| `datagen.cpp` | Self-play games for tuning |
| `wasm_api.cpp` | Functions exported to JavaScript in the WebAssembly build |

## 2. Representing the board

### Bitboards

A **bitboard** is a 64-bit integer with one bit per square (a1 is bit 0, h8 is
bit 63). Bastion keeps one bitboard per piece type and one per colour, plus a
64-entry array saying which piece stands on each square (the "mailbox", for quick
lookups).

The point of bitboards is that set operations on all 64 squares happen in one
CPU instruction:

```cpp
Bitboard whitePawns = pos.pieces(WHITE, PAWN);
Bitboard pushes     = (whitePawns << 8) & ~pos.occupied();  // every single pawn push at once
Bitboard attacked   = pawn_attacks_bb(WHITE, whitePawns);    // every square white pawns attack
int      count      = popcount(attacked);                    // hardware POPCNT
```

`pop_lsb` (count trailing zeros, then clear the lowest bit) walks through the set
squares of a bitboard, which is how lists of moves are produced.

### Attack tables and magic bitboards

Knights, kings and pawns always attack the same squares from a given square, so
their attacks are precomputed into 64-entry tables at startup.

Rooks, bishops and queens ("sliders") are harder: their attacks depend on which
squares are blocked. The trick used is **magic bitboards**:

1. For a rook on d4, only the squares on its rank and file (excluding the board
   edge) can block it. That *mask* has at most 12 bits.
2. Take the board's occupancy, keep only the masked bits, multiply by a
   carefully chosen 64-bit "magic" number and shift right. The result is a small
   index (at most 12 bits) that is unique for every arrangement of blockers that
   matters.
3. That index selects the precomputed attack set from a table.

```cpp
unsigned index = unsigned(((occupied & mask) * magic) >> shift);
Bitboard attacks = table[index];
```

The magic numbers are found at startup by trying random sparse numbers until one
produces no harmful collisions (`init_magics` in `bitboard.cpp`). With a fixed
random seed this is deterministic and takes a few milliseconds.

### State, keys and make/unmake

Each move pushes a `StateInfo` onto a stack: castling rights, the en passant
square, the 50-move counter, the captured piece, the hash key, which pieces give
check and which pieces are pinned. Taking a move back pops it and moves the
pieces back, which is cheaper than copying the whole board.

The **Zobrist key** identifies a position with a 64-bit number: every
(piece, square) pair, castling state, en passant file and the side to move has a
random 64-bit value, and the key is the XOR of the values that apply. Because XOR
is its own inverse, moving a knight from g1 to f3 updates the key with two XORs.
Keys index the transposition table and detect repetitions.

Repetitions are found by walking back through earlier states with the same side
to move, but never past the last capture or pawn move (those are irreversible).

## 3. Generating moves

`movegen.cpp` generates **pseudo-legal** moves in bulk, using bitboard shifts for
pawns and the attack tables for pieces. A move is pseudo-legal when it follows
the piece's movement rules but might leave the king in check.

Legality is checked cheaply afterwards (`Position::legal`):

- A king move is legal if the destination is not attacked (computed with the king
  removed from the board, so it cannot hide behind itself).
- Any other move is legal unless the piece is **pinned** and leaves the line
  between the pinning slider and the king. Pins are computed once per position.
- En passant is checked directly, because removing two pawns from the same rank
  can expose the king along that rank.

When the side to move is in check, a separate generator produces only
**evasions**: king moves, captures of the checking piece, and blocks on the
squares between. In double check only the king may move.

Moves are generated in groups (captures, quiet moves, evasions) so the search can
stop early: if a capture refutes the position, the quiet moves are never
generated at all.

### How we know it is correct: perft

`perft(depth)` counts every leaf of the legal move tree. These numbers are known
for standard test positions, and any bug (a missed en passant, castling through
check, a promotion that forgets to check) changes them. `tests/perft_tests.cpp`
checks 27 positions; the deep suite visits 761,234,712 positions and matches
every reference count. `tests/unit_tests.cpp` adds checks perft cannot catch:
incremental keys match keys computed from scratch, `gives_check` agrees with the
real result of every move, and all 65,536 possible 16-bit moves are accepted by
`pseudo_legal` exactly when they appear in the legal move list.

## 4. Searching

### Minimax, negamax and alpha-beta

The search assumes both sides play their best move. In **negamax** form a node's
score is the best of the negated scores of its children: what is good for me is
bad for my opponent by the same amount.

**Alpha-beta** keeps a window `[alpha, beta]`: `alpha` is the score I am already
guaranteed elsewhere, `beta` is the score my opponent is already guaranteed. If a
move scores at least `beta`, the opponent would never allow this position, so the
remaining moves are skipped (a **beta cutoff**). With perfect move ordering this
searches about the square root of the full tree, which roughly doubles the depth
reached in the same time.

### Principal variation search (PVS)

The first move at each node is searched with the full window. Every later move is
first searched with a **null window** `[alpha, alpha + 1]`, which only answers "is
this better than what I have?". That is much cheaper, and only if the answer is
yes is the move searched again with the full window.

### Iterative deepening and aspiration windows

Bastion searches to depth 1, then 2, then 3, and so on until time runs out. This
looks wasteful but is not: each iteration fills the hash table and history
tables, so the next one orders its moves much better, and there is always a
complete answer ready when time runs out.

From depth 4 each iteration starts with a narrow **aspiration window** around the
previous score. If the true score falls outside it, the window is widened and the
iteration repeated.

### Transposition table

Different move orders often reach the same position. The transposition table
(`tt.cpp`) is a large hash table indexed by the Zobrist key. Each 10-byte entry
stores the best move, the score, the static evaluation, the depth searched and the
**bound type**:

- *exact*: the score is the true value at that depth;
- *lower bound*: the search failed high (score ≥ beta), the true value may be higher;
- *upper bound*: it failed low (score ≤ alpha), the true value may be lower.

A stored result can end the search of a node immediately when it was searched at
least as deep and its bound is on the right side of the window. Even when it
cannot, its best move is tried first. Entries are grouped three to a 32-byte
cluster; when a cluster is full, the shallowest and oldest entry is replaced.

Mate scores are stored relative to the node rather than the root, so that "mate
in 3 from here" stays correct when the same position is reached at a different
distance from the root.

### Quiescence search

Stopping at a fixed depth in the middle of an exchange gives nonsense (you count
the queen you just captured but not the recapture). At depth 0, **quiescence
search** continues with captures only, until the position is quiet. The side to
move may also "stand pat" (decline to capture) if the static evaluation is
already good enough. Captures that lose material according to SEE, or that could
not raise the score to alpha even if they won the piece outright (delta pruning),
are skipped.

**Static exchange evaluation** (`Position::see_ge`) works out the result of a
sequence of captures on one square, always recapturing with the least valuable
piece and revealing x-ray attackers behind each piece that moves.

### Move ordering

Alpha-beta is only as good as its move ordering. `movepick.cpp` hands out moves in
stages:

1. the hash move;
2. captures that do not lose material, most valuable victim first, adjusted by a
   *capture history* of how well each capture worked before;
3. two **killer moves**: quiet moves that caused a cutoff at the same depth in a
   sibling position;
4. the **countermove**: the quiet move that last refuted the opponent's previous move;
5. remaining quiet moves sorted by **history**: a table, indexed by colour, from-square
   and to-square, rewarded when a move causes a cutoff and penalised when it fails.
   **Continuation history** does the same indexed by the previous one, two and four
   moves, capturing patterns like "after Nf6, Bg5 tends to be good";
6. captures that lose material.

History updates use a "gravity" formula, `h += bonus - h * |bonus| / 16384`, which
keeps values bounded and lets old information fade.

### Pruning and reductions

The tree grows exponentially, so most of the strength comes from not searching
lines that are almost certainly irrelevant. All of these are standard techniques
from the chess programming literature; `docs/TESTING.md` lists how much each one
is worth in Bastion, measured by switching it off.

| Technique | Idea |
| --- | --- |
| **Null move pruning** | Let the opponent move twice. If we still beat beta after a reduced search, a real move would too. Disabled in pawn endgames, where passing can be an advantage (zugzwang), and verified with a real search at high depth. |
| **Reverse futility pruning** | At low depth, if the static evaluation beats beta by a depth-dependent margin, assume the node fails high. |
| **Razoring** | At very low depth, if the evaluation is far below alpha, ask quiescence search whether captures can save the position. |
| **Late move reductions** | Moves late in the ordering are searched with less depth, using a reduction that grows with the logarithm of depth and move number and shrinks for moves with good history, killers and checks. A move that does better than expected is searched again at full depth. |
| **Late move pruning** | At low depth, stop searching quiet moves after a certain number have been tried. |
| **Futility pruning** | Skip quiet moves when the static evaluation plus a margin is still below alpha. |
| **SEE pruning** | Skip moves that lose too much material at low depth. |
| **History pruning** | Skip quiet moves whose history score is very negative. |
| **Internal iterative reduction** | If there is no hash move, search one ply shallower: the node is probably not important. |
| **Check extension** | Search one ply deeper when in check, so forcing lines are not cut off early. |
| **Singular extension** | If the hash move is much better than every alternative (shown by a reduced search that excludes it), search it deeper. If several moves beat beta instead, cut the node immediately ("multi-cut"). |
| **Mate distance pruning** | Never search for a mate longer than one already found. |

### Draws

A position that repeats a position from earlier in the *search* is scored as a
draw immediately (the side that could avoid it would have), while repetitions of
positions from before the search began must occur three times. The 50-move rule
and dead positions (king against king and a minor piece, or same-coloured
bishops) are also draws. Draw scores get a tiny random component so the engine
does not blindly shuffle into repetitions.

### Time management

With a clock, Bastion computes a soft limit (about 3% of the remaining time plus
half the increment) and a hard limit (about five times larger, but never more than
70% of the remaining time). After each iteration it decides whether to start another:

- if the best move has been the same for several iterations, it stops early;
- if most of the search effort went into the best move, it stops early, because
  the alternatives were refuted quickly;
- if the best move keeps changing or the score is dropping, it thinks longer.

The hard limit is checked every 1,024 nodes.

### Threads (Lazy SMP)

With `Threads > 1`, several threads search the same position independently and
share only the transposition table. They naturally drift apart (different timing
means different hash hits), and the extra information they leave in the table
helps the main thread. It is simple and scales reasonably to a handful of cores.
Writes to the table are not locked; a corrupted entry is harmless because hash
moves are always validated with `pseudo_legal` before use.

## 5. Evaluating positions

`evaluate.cpp` scores a position from the point of view of the side to move.
Every term is a pair of numbers, one for the middlegame and one for the endgame,
packed into a single integer so they can be added together. The final score
blends the two according to the material left:

```
phase = knights + bishops + 2 * rooks + 4 * queens   (24 at the start)
score = (mg * phase + eg * (24 - phase)) / 24
```

The terms:

- **Material and piece-square tables**: a value for every piece on every square.
- **Mobility**: how many safe squares each piece can reach.
- **Pawn structure**: doubled, isolated, backward and connected pawns.
- **Passed pawns**: by rank, whether the path is free, and how close each king is.
- **King safety**: pawn shelter and enemy pawn storms, attackers on the zone
  around the king, safe checking squares, an open file in front of the king.
- **Pieces**: bishop pair, outposts, bishops blocked by their own pawns, rooks on
  open files and the seventh rank.
- **Threats**: pieces attacked by pawns, minor pieces or rooks, and hanging pieces.
- **Tempo**: a small bonus for having the move.

Hard-to-win endgames (no pawns and less than a rook ahead; opposite-coloured
bishops) are scaled toward a draw.

## 6. Tuning the evaluation

The evaluation describes *what* to look at. *How much* each feature is worth was
learned from data rather than guessed, using Texel's method:

1. **Data generation** (`datagen` command): Bastion plays games against itself
   from random openings, searching 5,000 nodes per move. Quiet positions (not in
   check, best move not a capture) are saved together with the final result.
2. **Tracing**: for each position the tuner records how often each weight is
   used by each side. Because every term is a weight times a count, the
   evaluation becomes a linear function of the weights.
3. **Fitting**: the predicted result is `sigmoid(K * eval)`. The tuner first
   picks `K` so the existing weights fit best, then minimises the mean squared
   error between predictions and real results over all positions with full-batch
   gradient descent (the Adam optimiser).
4. The new weights are written back to `src/eval_params.h`, and the tuned engine
   must then beat the old one in real games before the change is kept.

The initial weights were the PeSTO piece-square tables published by Ronald
Friederich, with every other term at zero.

## 7. Testing

| What | How |
| --- | --- |
| Move generation | perft suite (27 positions), run on every build |
| Board logic | unit tests: hashing, check detection, move validation, SEE, draw rules, malformed FEN |
| Memory safety | the whole test suite under AddressSanitizer and UndefinedBehaviorSanitizer |
| Untrusted input | libFuzzer harnesses for the FEN parser and UCI commands |
| Determinism | `bench` searches 36 positions to a fixed depth; commits that change the search record the node count in their message, and CI checks it |
| Playing strength | games between versions (SPRT) and against reference engines (gauntlets), run with fastchess |
| Website engine | the WebAssembly build is tested under Node before each deployment |

**SPRT** (sequential probability ratio test) decides between two hypotheses, for
example "the change gains at least 5 Elo" against "it gains nothing", with fixed
error rates. It plays games until the log-likelihood ratio crosses one of two
bounds, so clear results finish quickly and borderline ones take longer.

**Elo** differences translate to expected scores: a player rated 100 points
higher is expected to score about 64%. Bastion's rating was estimated from games
against reference engines with known ratings on the CCRL blitz scale (Stash 25,
and Stockfish held back to calibrated skill levels); details and confidence
intervals are in `docs/TESTING.md`.

## 8. The website and WebAssembly

The same C++ source compiles to WebAssembly (`make wasm`) with clang targeting
WASI, a minimal system interface. The browser build has no threads, so a search
runs to completion inside a **Web Worker** and the page stays responsive.
`web/js/engine-core.js` provides the handful of system calls the engine needs
(a clock and console output) and wraps the exported functions; JSON passes
between C++ and JavaScript.

The page never implements chess rules itself: legal moves, move names (SAN),
check and game results all come from the engine (`bastion_state`), so the website
and the desktop engine cannot disagree. The search streams progress back while it
runs (`js_emit`), which is how the page draws the line Bastion expects. Weaker
levels limit the search depth and add noise to the evaluation.

## 9. Playing on Lichess

Lichess lets programs play through its Bot API. `lichess-bot` (an open-source
bridge) logs in with a bot account's token, accepts challenges and talks to
Bastion over UCI. `bot/config.yml.example` is a ready configuration and
`bot/README.md` the setup guide.

## 10. Questions you should be able to answer

**Why bitboards instead of an 8×8 array?** Set operations on all squares at once:
generating all pawn pushes is one shift, and "is this square attacked?" is a few
table lookups instead of loops.

**How do magic bitboards work?** Mask the occupancy to the squares that can block
the slider, multiply by a magic constant and shift: the result is a perfect hash
of the blocker arrangement into a table of attack sets.

**How do you know the move generator is correct?** Perft counts match published
values for 27 positions (over 800 million leaf nodes), and unit tests cross-check
incremental hashing, check detection and move validation.

**What does alpha-beta gain over minimax?** With good move ordering it searches
about the square root of the tree, roughly doubling the reachable depth, and it
returns exactly the same result as minimax.

**Why can null move pruning fail?** In zugzwang, where any move makes the position
worse, passing would be the best option and the null move gives a wrong
fail-high. Bastion disables it without pieces other than pawns and verifies it at
high depth.

**What is the horizon effect and how is it handled?** A search cut off at a fixed
depth can push an inevitable loss just past its horizon. Quiescence search and
check extensions reduce it.

**What goes in the transposition table and why are bounds needed?** Alpha-beta
scores are often only bounds (fail high or fail low), so the entry records whether
its score is exact, a lower bound or an upper bound, and it is only used to cut
off when it is valid for the current window.

**How do you know a change makes the engine stronger?** Play it against the
previous version until the result is statistically clear. An SPRT stops as soon as
it can tell a real gain from noise; small gains need thousands of games. The bench
node count shows which changes alter the search at all.

**How did you choose the evaluation weights?** Texel tuning: self-play data,
a logistic model of the result, gradient descent on the mean squared error.

**Why run the engine in a Web Worker?** The search runs for seconds at a time; on
the main thread it would freeze the page. A worker also keeps the UI code
separate from the engine.

**What would you do next?** Replace the hand-written evaluation with a small
neural network (NNUE) trained on the same kind of self-play data, add Syzygy
tablebase probing for endgames, and support Chess960.
