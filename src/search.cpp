// search.cpp: the search.
//
// The core is a negamax alpha-beta search using principal variation search
// (PVS): the first move at each node is searched with a full window, the rest
// with a null window that is cheaper and only re-searched if it surprises us.
// Around it sit the techniques that make modern engines strong:
//   - iterative deepening with aspiration windows
//   - a transposition table for cutoffs and move ordering
//   - quiescence search, so evaluation only happens in quiet positions
//   - pruning of hopeless branches: reverse futility, null move, razoring,
//     late move pruning, futility and SEE pruning
//   - late move reductions: later (less promising) moves are searched less deeply
//   - move ordering from killer moves, countermoves and history tables
#include "search.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "evaluate.h"
#include "movepick.h"

namespace bastion {

ThreadPool Threads;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

namespace {

int Reductions[2][MAX_PLY + 1][MAX_MOVES];  // [is quiet][depth][move number]

constexpr int PieceValueSimple[7] = {100, 320, 330, 500, 950, 0, 0};

// A tiny random component stops the engine from blindly walking into repetitions.
Value draw_value(std::uint64_t nodes) { return VALUE_DRAW - 1 + int(nodes & 2); }

int history_bonus(int depth) { return std::min(170 * depth - 100, 1600); }

// History "gravity": entries saturate smoothly toward +/-HistoryMax.
void apply_bonus(std::int16_t& entry, int bonus) {
    bonus = std::clamp(bonus, -HistoryMax, HistoryMax);
    entry = std::int16_t(entry + bonus - entry * std::abs(bonus) / HistoryMax);
}

}  // namespace

void Search::init() {
    for (int d = 1; d <= MAX_PLY; ++d)
        for (int m = 1; m < MAX_MOVES; ++m) {
            double l             = std::log(d) * std::log(m);
            Reductions[0][d][m] = int(0.20 + l / 3.35);
            Reductions[1][d][m] = int(0.75 + l / 2.25);
        }
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void Worker::clear() {
    std::memset(mainHistory, 0, sizeof(mainHistory));
    std::memset(captureHistory, 0, sizeof(captureHistory));
    std::memset(contHist, 0, sizeof(contHist));
    for (auto& row : counterMoves) std::fill(std::begin(row), std::end(row), Move::none());
}

Value Worker::evaluate(const Position& pos) const {
    Value v     = Eval::evaluate(pos);
    int   skill = Threads.options.skill;
    if (skill < 20) {
        // Weaker play for the website's easier levels: deterministic noise per position.
        std::uint64_t h   = (pos.key() ^ Threads.noiseSeed) * 0x9E3779B97F4A7C15ULL;
        int           amp = (20 - skill) * 14;
        v += int((h >> 40) % std::uint64_t(2 * amp + 1)) - amp;
    }
    return v;
}

bool Worker::should_stop() {
    if (Threads.stopFlag.load(std::memory_order_relaxed)) return true;
    if (id != 0 || (node_count() & 1023) || !completedDepth) return false;

    const Search::Limits& lim = Threads.limits;
    if ((lim.nodes && Threads.nodes_searched() >= lim.nodes) ||
        ((lim.use_time_management() || lim.movetime) && !lim.infinite && Threads.elapsed() >= Threads.maximumTime)) {
        Threads.stopFlag = true;
        return true;
    }
    return false;
}

void Worker::update_pv(int ply, Move m) {
    pvTable[ply][ply] = m;
    for (int i = ply + 1; i < pvLength[ply + 1]; ++i) pvTable[ply][i] = pvTable[ply + 1][i];
    pvLength[ply] = pvLength[ply + 1];
}

void Worker::update_continuation(Stack* ss, Piece pc, Square to, int bonus) {
    for (int i : {1, 2, 4}) {
        if (!(ss - i)->currentMove.is_ok()) continue;
        apply_bonus((*(ss - i)->contHist)[pc][to], i == 4 ? bonus / 2 : bonus);
    }
}

void Worker::update_quiet_stats(const Position& pos, Stack* ss, Move best, int bonus, const Move* quiets,
                                int quietCount) {
    const Color us = pos.side_to_move();
    if (ss->killers[0] != best) {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = best;
    }
    if ((ss - 1)->currentMove.is_ok()) {
        Square prevSq                               = (ss - 1)->currentMove.to();
        counterMoves[pos.piece_on(prevSq)][prevSq] = best;
    }
    apply_bonus(mainHistory[us][best.from()][best.to()], bonus);
    update_continuation(ss, pos.moved_piece(best), best.to(), bonus);
    // Quiet moves tried before the cutoff move were worse: lower their scores.
    for (int i = 0; i < quietCount; ++i) {
        apply_bonus(mainHistory[us][quiets[i].from()][quiets[i].to()], -bonus);
        update_continuation(ss, pos.moved_piece(quiets[i]), quiets[i].to(), -bonus);
    }
}

void Worker::update_capture_stats(const Position& pos, Move best, int bonus, const Move* captures,
                                  int captureCount) {
    auto entry = [&](Move m) -> std::int16_t& {
        PieceType captured = m.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(m.to()));
        return captureHistory[pos.moved_piece(m)][m.to()][captured];
    };
    if (best && pos.is_tactical(best)) apply_bonus(entry(best), bonus);
    for (int i = 0; i < captureCount; ++i) apply_bonus(entry(captures[i]), -bonus);
}

// ---------------------------------------------------------------------------
// Main search
// ---------------------------------------------------------------------------
template <bool PvNode>
Value Worker::search(Position& pos, Stack* ss, Value alpha, Value beta, int depth, bool cutNode) {
    const bool rootNode = PvNode && ss->ply == 0;
    const bool inCheck  = pos.in_check();

    // Check extension: never stop searching while in check.
    if (inCheck && depth < MAX_PLY) ++depth;
    if (depth <= 0) return qsearch<PvNode>(pos, ss, alpha, beta);

    if (PvNode) pvLength[ss->ply] = ss->ply;
    count_node();
    if (should_stop()) return 0;
    if (PvNode && selDepth < ss->ply + 1) selDepth = ss->ply + 1;

    if (!rootNode) {
        if (pos.is_draw(ss->ply)) return draw_value(node_count());
        if (ss->ply >= MAX_PLY - 1) return inCheck ? 0 : evaluate(pos);

        // Mate distance pruning: no line can be better than the shortest mate already found.
        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta) return alpha;
    }

    const Color us       = pos.side_to_move();
    const Move  excluded = ss->excludedMove;
    ss->inCheck          = inCheck;
    ss->moveCount        = 0;
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = Move::none();
    (ss + 1)->excludedMove                    = Move::none();

    // --- Transposition table lookup ---
    const Key key = pos.key() ^ (excluded ? Key(excluded.raw()) * 0x9E3779B97F4A7C15ULL : 0);
    bool      ttHit;
    TTEntry*  tte     = TT.probe(key, ttHit);
    Value     ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;
    Move      ttMove  = rootNode ? (bestMove ? bestMove : (ttHit ? tte->move() : Move::none()))
                                 : (ttHit ? tte->move() : Move::none());
    const bool ttPv   = PvNode || (ttHit && tte->is_pv());
    ss->ttPv          = ttPv;

    if (!PvNode && !excluded && ttHit && ttValue != VALUE_NONE && tte->depth() >= depth &&
        (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)) && pos.rule50_count() < 90) {
        // A quiet hash move that still produces a cutoff deserves credit.
        if (ttMove && ttValue >= beta && !pos.is_tactical(ttMove) && pos.pseudo_legal(ttMove))
            apply_bonus(mainHistory[us][ttMove.from()][ttMove.to()], history_bonus(depth));
        return ttValue;
    }

    // --- Static evaluation ---
    Value rawEval = VALUE_NONE, eval = VALUE_NONE;
    bool  improving = false;

    if (inCheck) {
        ss->staticEval = VALUE_NONE;
    } else {
        if (excluded)
            rawEval = ss->staticEval;
        else if (ttHit && tte->eval() != VALUE_NONE)
            rawEval = tte->eval();
        else
            rawEval = evaluate(pos);
        ss->staticEval = eval = rawEval;

        // A hash score is a better estimate than the static evaluation when its bound allows it.
        if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER))) eval = ttValue;

        if (!ttHit && !excluded)
            tte->save(key, VALUE_NONE, ttPv, BOUND_NONE, -1, Move::none(), rawEval, TT.generation());

        // "Improving": is our position better than two plies ago? Prune less when it is.
        if ((ss - 2)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 2)->staticEval;
        else if ((ss - 4)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 4)->staticEval;
        else improving = true;
    }

    if (!PvNode && !inCheck && !excluded) {
        // Reverse futility pruning: far above beta at low depth, assume the opponent can't recover.
        if (depth <= 8 && eval - 80 * (depth - improving) >= beta && eval < VALUE_MATE_IN_MAX_PLY)
            return eval;

        // Razoring: hopelessly below alpha, check whether captures can save us.
        if (depth <= 3 && eval + 250 + 200 * depth <= alpha) {
            Value v = qsearch<false>(pos, ss, alpha, alpha + 1);
            if (v <= alpha) return v;
        }

        // Null move pruning: if passing still beats beta, a real move almost certainly will.
        if (depth >= 3 && eval >= beta && ss->staticEval >= beta - 20 * depth + 160 &&
            (ss - 1)->currentMove != Move::null() && pos.has_non_pawn_material(us) && ss->ply >= nmpMinPly &&
            beta > VALUE_MATED_IN_MAX_PLY) {
            int R = 4 + depth / 3 + std::min((eval - beta) / 200, 3);

            ss->currentMove = Move::null();
            ss->contHist    = &contHist[NO_PIECE][0];
            pos.do_null_move();
            Value nullValue = -search<false>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
            pos.undo_null_move();

            if (Threads.stopFlag.load(std::memory_order_relaxed)) return 0;
            if (nullValue >= beta) {
                if (nullValue >= VALUE_MATE_IN_MAX_PLY) nullValue = beta;
                if (depth < 14 || nmpMinPly) return nullValue;

                // At high depth, verify with a real search (guards against zugzwang).
                nmpMinPly = ss->ply + 3 * (depth - R) / 4;
                Value v   = search<false>(pos, ss, beta - 1, beta, depth - R, false);
                nmpMinPly = 0;
                if (v >= beta) return nullValue;
            }
        }
    }

    // Internal iterative reduction: without a hash move this node is probably less important.
    if (!inCheck && !ttMove && depth >= 4 && (PvNode || cutNode)) --depth;

    // --- Move loop ---
    MovePicker mp(pos, ttMove, depth, *this, ss);
    Value      bestValue = -VALUE_INFINITE;
    Move       best      = Move::none();
    Move       quietsSearched[64], capturesSearched[32];
    int        quietCount = 0, captureCount = 0, moveCount = 0;
    bool       skipQuiets = false;
    Move       m;

    while ((m = mp.next_move(skipQuiets)) != Move::none()) {
        if (m == excluded) continue;
        if (rootNode && std::find(rootMoves.begin(), rootMoves.end(), m) == rootMoves.end()) continue;
        if (!pos.legal(m)) continue;

        ss->moveCount = ++moveCount;

        const bool  tactical   = pos.is_tactical(m);
        const bool  givesCheck = pos.gives_check(m);
        const Piece moved      = pos.moved_piece(m);
        const int   history    = tactical ? 0
                                          : mainHistory[us][m.from()][m.to()] + (*(ss - 1)->contHist)[moved][m.to()] +
                                           (*(ss - 2)->contHist)[moved][m.to()];
        int newDepth = depth - 1;

        // --- Shallow depth pruning ---
        if (!rootNode && bestValue > VALUE_MATED_IN_MAX_PLY && pos.has_non_pawn_material(us)) {
            const int lmrDepth = std::max(0, newDepth - Reductions[!tactical][depth][std::min(moveCount, MAX_MOVES - 1)]);
            if (!tactical) {
                // Late move pruning: after enough quiet moves, the rest are unlikely to matter.
                if (moveCount >= (3 + depth * depth) / (2 - improving)) {
                    skipQuiets = true;
                    continue;
                }
                // Futility pruning: even a good quiet move won't lift us to alpha.
                if (!inCheck && !givesCheck && lmrDepth <= 8 && ss->staticEval + 110 + 100 * lmrDepth <= alpha)
                    continue;
                // History pruning: moves that have consistently failed before.
                if (lmrDepth <= 3 && history < -2500 * depth) continue;
                // SEE pruning: quiet moves that hang material.
                if (!pos.see_ge(m, -25 * lmrDepth * lmrDepth)) continue;
            } else if (depth <= 8 && !pos.see_ge(m, -95 * depth))
                continue;  // losing captures at low depth
        }

        // --- Singular extension: if the hash move is much better than all alternatives, search it deeper ---
        int extension = 0;
        if (!rootNode && m == ttMove && !excluded && depth >= 7 && ttValue != VALUE_NONE &&
            !is_mate_score(ttValue) && (tte->bound() & BOUND_LOWER) && tte->depth() >= depth - 3 &&
            ss->ply < 2 * rootDepth) {
            const Value singularBeta  = ttValue - 2 * depth;
            const int   singularDepth = (depth - 1) / 2;
            ss->excludedMove          = m;
            Value v = search<false>(pos, ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
            ss->excludedMove = Move::none();
            if (Threads.stopFlag.load(std::memory_order_relaxed)) return 0;

            if (v < singularBeta) extension = (!PvNode && v < singularBeta - 20) ? 2 : 1;
            else if (singularBeta >= beta) return singularBeta;  // multi-cut: several moves beat beta
            else if (ttValue >= beta) extension = -1;
        }
        newDepth += extension;

        // --- Make the move ---
        ss->currentMove              = m;
        ss->contHist                 = &contHist[moved][m.to()];
        const std::uint64_t nodesBefore = node_count();
        TT.prefetch(pos.key_after(m));
        pos.do_move(m);

        Value value = -VALUE_INFINITE;

        // Late move reductions: search later moves shallower with a null window first.
        if (depth >= 2 && moveCount > 1 + rootNode && (!tactical || !ttPv)) {
            int r = Reductions[!tactical][depth][std::min(moveCount, MAX_MOVES - 1)];
            if (!tactical) {
                r += !improving;
                r += cutNode;
                r -= givesCheck;
                r -= (m == ss->killers[0] || m == ss->killers[1]);
                r -= std::clamp(history / 8000, -2, 2);
            }
            r -= ttPv;
            r -= PvNode;

            const int d = std::clamp(newDepth - r, 1, newDepth + 1);
            value       = -search<false>(pos, ss + 1, -(alpha + 1), -alpha, d, true);
            if (value > alpha && d < newDepth)
                value = -search<false>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);
        } else if (!PvNode || moveCount > 1) {
            value = -search<false>(pos, ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);
        }

        // Full window search for the first move and for moves that beat alpha in a PV node.
        if (PvNode && (moveCount == 1 || (value > alpha && (rootNode || value < beta))))
            value = -search<true>(pos, ss + 1, -beta, -alpha, newDepth, false);

        pos.undo_move(m);

        if (Threads.stopFlag.load(std::memory_order_relaxed)) return 0;
        if (rootNode) rootMoveNodes[m.from()][m.to()] += node_count() - nodesBefore;

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                best = m;
                if (PvNode) update_pv(ss->ply, m);
                if (rootNode) iterBestMove = m;
                if (value >= beta) break;  // fail high: the opponent will avoid this line
                alpha = value;
            }
        }

        if (m != best) {
            if (tactical && captureCount < 32) capturesSearched[captureCount++] = m;
            else if (!tactical && quietCount < 64) quietsSearched[quietCount++] = m;
        }
    }

    if (!moveCount) return excluded ? alpha : inCheck ? mated_in(ss->ply) : VALUE_DRAW;

    // --- Learn from the result ---
    if (best && bestValue >= beta) {
        const int bonus = history_bonus(depth + (bestValue > beta + 100));
        if (!pos.is_tactical(best)) update_quiet_stats(pos, ss, best, bonus, quietsSearched, quietCount);
        update_capture_stats(pos, best, bonus, capturesSearched, captureCount);
    }

    if (!excluded) {
        const Bound bound = bestValue >= beta ? BOUND_LOWER : (PvNode && best) ? BOUND_EXACT : BOUND_UPPER;
        tte->save(key, value_to_tt(bestValue, ss->ply), ttPv, bound, depth, best, rawEval, TT.generation());
    }
    return bestValue;
}

// ---------------------------------------------------------------------------
// Quiescence search: only captures (and check evasions), so that the static
// evaluation is never trusted in the middle of an exchange.
// ---------------------------------------------------------------------------
template <bool PvNode>
Value Worker::qsearch(Position& pos, Stack* ss, Value alpha, Value beta) {
    if (PvNode) {
        pvLength[ss->ply] = ss->ply;
        if (selDepth < ss->ply + 1) selDepth = ss->ply + 1;
    }
    count_node();
    if (should_stop()) return 0;
    if (pos.is_draw(ss->ply)) return draw_value(node_count());

    const bool inCheck = pos.in_check();
    if (ss->ply >= MAX_PLY - 1) return inCheck ? 0 : evaluate(pos);

    bool       ttHit;
    const Key  key     = pos.key();
    TTEntry*   tte     = TT.probe(key, ttHit);
    const Value ttValue = ttHit ? value_from_tt(tte->value(), ss->ply) : VALUE_NONE;
    const Move ttMove  = ttHit ? tte->move() : Move::none();
    const bool pvHit   = ttHit && tte->is_pv();

    if (!PvNode && ttHit && ttValue != VALUE_NONE && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    Value bestValue, rawEval = VALUE_NONE, futilityBase = -VALUE_INFINITE;
    if (inCheck) {
        ss->staticEval = VALUE_NONE;
        bestValue      = -VALUE_INFINITE;
    } else {
        rawEval        = ttHit && tte->eval() != VALUE_NONE ? tte->eval() : evaluate(pos);
        ss->staticEval = bestValue = rawEval;
        if (ttValue != VALUE_NONE && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
            bestValue = ttValue;

        // Stand pat: the side to move can usually do at least as well as doing nothing.
        if (bestValue >= beta) {
            if (!ttHit)
                tte->save(key, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER, 0, Move::none(), rawEval,
                          TT.generation());
            return bestValue;
        }
        alpha        = std::max(alpha, bestValue);
        futilityBase = ss->staticEval + 180;
    }

    MovePicker mp(pos, ttMove, *this, ss);
    Move       best = Move::none(), m;
    int        moveCount = 0;

    while ((m = mp.next_move()) != Move::none()) {
        if (!pos.legal(m)) continue;
        ++moveCount;

        if (!inCheck && bestValue > VALUE_MATED_IN_MAX_PLY) {
            if (!pos.gives_check(m) && m.type() != PROMOTION) {
                // Delta pruning: even winning the captured piece for free cannot reach alpha.
                PieceType captured = m.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(m.to()));
                Value     futility = futilityBase + PieceValueSimple[captured];
                if (futility <= alpha) {
                    bestValue = std::max(bestValue, futility);
                    continue;
                }
                if (futilityBase <= alpha && !pos.see_ge(m, 1)) {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }
            }
            if (!pos.see_ge(m, -50)) continue;  // skip clearly losing captures
        }

        ss->currentMove = m;
        ss->contHist    = &contHist[pos.moved_piece(m)][m.to()];
        TT.prefetch(pos.key_after(m));
        pos.do_move(m);
        Value value = -qsearch<PvNode>(pos, ss + 1, -beta, -alpha);
        pos.undo_move(m);

        if (Threads.stopFlag.load(std::memory_order_relaxed)) return 0;

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                best = m;
                if (PvNode) update_pv(ss->ply, m);
                if (value >= beta) break;
                alpha = value;
            }
        }
    }

    if (inCheck && !moveCount) return mated_in(ss->ply);

    tte->save(key, value_to_tt(bestValue, ss->ply), pvHit, bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, 0, best,
              rawEval, TT.generation());
    return bestValue;
}

// ---------------------------------------------------------------------------
// Iterative deepening
// ---------------------------------------------------------------------------
void Worker::iterative_deepening() {
    Position& pos = rootPos;
    Stack     stack[MAX_PLY + 10];
    std::memset(static_cast<void*>(stack), 0, sizeof(stack));
    Stack* ss = stack + 7;
    for (int i = -7; i <= MAX_PLY + 2; ++i) {
        (ss + i)->contHist   = &contHist[NO_PIECE][0];
        (ss + i)->staticEval = VALUE_NONE;
        if (i >= 0) (ss + i)->ply = i;
    }

    const bool isMain       = id == 0;
    Move       prevBestMove = Move::none();
    Value      prevScore    = VALUE_NONE;
    int        stability    = 0;

    for (rootDepth = 1; rootDepth < MAX_PLY - 8; ++rootDepth) {
        if (Threads.limits.depth && rootDepth > Threads.limits.depth) break;
        if (Threads.stopFlag.load(std::memory_order_relaxed)) break;

        selDepth     = 0;
        iterBestMove = Move::none();

        // Aspiration window around the previous score: narrow windows are faster.
        Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE, delta = 0, value = 0;
        if (rootDepth >= 4 && completedDepth) {
            delta = 12 + bestValue * bestValue / 15000;
            alpha = std::max(bestValue - delta, -VALUE_INFINITE);
            beta  = std::min(bestValue + delta, VALUE_INFINITE);
        }
        int failHighs = 0;
        while (true) {
            value = search<true>(pos, ss, alpha, beta, std::max(1, rootDepth - failHighs), false);
            if (Threads.stopFlag.load(std::memory_order_relaxed)) break;

            if (value <= alpha) {
                beta      = (alpha + beta) / 2;
                alpha     = std::max(value - delta, -VALUE_INFINITE);
                failHighs = 0;
            } else if (value >= beta) {
                beta = std::min(value + delta, VALUE_INFINITE);
                failHighs = std::min(failHighs + 1, 3);
            } else
                break;
            delta += delta / 2;
            if (delta > 800) alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
        }

        if (Threads.stopFlag.load(std::memory_order_relaxed)) {
            // An unfinished iteration can still have found a better root move.
            if (iterBestMove && completedDepth) {
                bestMove = iterBestMove;
                bestPv.assign(pvTable[0], pvTable[0] + pvLength[0]);
            }
            break;
        }

        completedDepth = rootDepth;
        bestValue      = value;
        bestMove       = pvTable[0][0];
        bestSelDepth   = selDepth;
        bestPv.assign(pvTable[0], pvTable[0] + pvLength[0]);

        if (!isMain) continue;

        if (Threads.onInfo && !Threads.options.quiet) {
            Search::Info info;
            info.depth    = rootDepth;
            info.seldepth = selDepth;
            info.score    = value;
            info.nodes    = Threads.nodes_searched();
            info.timeMs   = Threads.elapsed();
            info.hashfull = TT.hashfull();
            info.pv       = bestPv;
            Threads.onInfo(info);
        }

        const Search::Limits& lim = Threads.limits;
        if (lim.use_time_management() && !lim.infinite) {
            // Stop early when the best move is stable and took most of the effort;
            // think longer when it keeps changing or the score is dropping.
            stability = bestMove == prevBestMove ? std::min(stability + 1, 4) : 0;
            static constexpr double StabilityScale[5] = {2.2, 1.45, 1.1, 0.9, 0.8};
            const double            bestNodes = double(rootMoveNodes[bestMove.from()][bestMove.to()]);
            const double            frac      = bestNodes / double(std::max<std::uint64_t>(1, node_count()));
            const double            nodeScale = (1.5 - frac) * 1.35;
            const double scoreScale = prevScore == VALUE_NONE ? 1.0 : std::clamp(1.0 + (prevScore - value) / 120.0, 1.0, 1.6);
            const double soft = double(Threads.optimumTime) * StabilityScale[stability] * nodeScale * scoreScale;
            if (double(Threads.elapsed()) >= std::min(soft, double(Threads.maximumTime))) break;

            // A forced mate has been found and fully verified.
            if (value >= VALUE_MATE_IN_MAX_PLY && rootDepth >= 2 * (VALUE_MATE - value) + 4) break;
        }
        prevBestMove = bestMove;
        prevScore    = value;
    }
}

// ---------------------------------------------------------------------------
// Thread pool
// ---------------------------------------------------------------------------
ThreadPool::ThreadPool() { workers.push_back(std::make_unique<Worker>(0)); }

ThreadPool::~ThreadPool() {
    stop();
    wait();
}

void ThreadPool::set_threads(int n) {
    wait();
#ifdef BASTION_NO_THREADS
    n = 1;
#endif
    n               = std::clamp(n, 1, 256);
    options.threads = n;
    while (int(workers.size()) < n) workers.push_back(std::make_unique<Worker>(int(workers.size())));
    while (int(workers.size()) > n) workers.pop_back();
}

void ThreadPool::clear() {
    wait();
    for (auto& w : workers) w->clear();
}

std::uint64_t ThreadPool::nodes_searched() const {
    std::uint64_t total = 0;
    for (const auto& w : workers) total += w->node_count();
    return total;
}

std::int64_t ThreadPool::elapsed() const { return now_ms() - startTime; }

void ThreadPool::init_time(Color us) {
    optimumTime = maximumTime = 0;
    const std::int64_t overhead = options.moveOverhead;
    if (limits.movetime) {
        optimumTime = maximumTime = std::max<std::int64_t>(1, limits.movetime - overhead);
        return;
    }
    if (!limits.use_time_management()) return;

    const std::int64_t time = limits.time[us], inc = limits.inc[us];
    const std::int64_t safe = std::max<std::int64_t>(1, time - overhead);
    const int          mtg  = limits.movestogo ? std::clamp(limits.movestogo, 1, 40) : 22;
    const std::int64_t base = time / mtg + inc * 3 / 4;
    optimumTime             = std::max<std::int64_t>(1, std::min(base * 65 / 100, safe * 7 / 10));
    maximumTime             = std::max<std::int64_t>(1, std::min(base * 35 / 10, safe * 7 / 10));
}

void ThreadPool::start(const Position& pos, const Search::Limits& lim) {
    wait();
    stopFlag  = false;
    limits    = lim;
    startTime = now_ms();
    noiseSeed = std::uint64_t(startTime) * 0x2545F4914F6CDD1DULL;
    init_time(pos.side_to_move());
    TT.new_search();

    std::vector<Move> rootMoves;
    for (const ExtMove& m : MoveList<LEGAL>(pos))
        if (limits.searchmoves.empty() ||
            std::find(limits.searchmoves.begin(), limits.searchmoves.end(), m.move) != limits.searchmoves.end())
            rootMoves.push_back(m.move);

    for (auto& w : workers) {
        w->rootPos   = pos;
        w->rootMoves = rootMoves;
        w->nodes.store(0, std::memory_order_relaxed);
        w->completedDepth = 0;
        w->bestMove       = Move::none();
        w->bestValue      = -VALUE_INFINITE;
        w->bestPv.clear();
        w->iterBestMove = Move::none();
        w->nmpMinPly    = 0;
        std::memset(w->rootMoveNodes, 0, sizeof(w->rootMoveNodes));
    }

    isSearching = true;
#ifdef BASTION_NO_THREADS
    run_main();
#else
    mainThread = std::thread(&ThreadPool::run_main, this);
#endif
}

void ThreadPool::run_main() {
    Worker& m = main();
    if (m.rootMoves.empty()) {
        // Checkmate or stalemate: there is nothing to search.
        if (onInfo && !options.quiet) {
            Search::Info info;
            info.score = m.rootPos.in_check() ? mated_in(0) : VALUE_DRAW;
            onInfo(info);
        }
        if (onBestMove && !options.quiet) onBestMove(Move::none(), Move::none());
        isSearching = false;
        return;
    }

#ifndef BASTION_NO_THREADS
    for (std::size_t i = 1; i < workers.size(); ++i)
        helperThreads.emplace_back(&Worker::iterative_deepening, workers[i].get());
#endif

    m.iterative_deepening();

#ifndef BASTION_NO_THREADS
    // "go infinite" must not report a move until the GUI says "stop".
    while (limits.infinite && !stopFlag) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    stopFlag = true;

#ifndef BASTION_NO_THREADS
    for (auto& t : helperThreads) t.join();
    helperThreads.clear();
#endif

    // Prefer a helper's result if it completed a deeper iteration with a better score.
    Worker* best = &m;
    for (auto& w : workers)
        if (w->bestMove && w->completedDepth > best->completedDepth && w->bestValue > best->bestValue) best = w.get();

    Move bestMove = best->bestMove ? best->bestMove : m.rootMoves[0];
    Move ponder   = best->bestPv.size() > 1 && best->bestPv[0] == bestMove ? best->bestPv[1] : Move::none();
    lastBestMove = bestMove;
    if (onBestMove && !options.quiet) onBestMove(bestMove, ponder);
    isSearching = false;
}

void ThreadPool::wait() {
#ifndef BASTION_NO_THREADS
    if (mainThread.joinable()) mainThread.join();
#endif
}

template Value Worker::search<true>(Position&, Stack*, Value, Value, int, bool);
template Value Worker::search<false>(Position&, Stack*, Value, Value, int, bool);

}  // namespace bastion
