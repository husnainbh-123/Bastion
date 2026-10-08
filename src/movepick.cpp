// movepick.cpp: staged move ordering.
//
// Order in the main search:
//   1. hash move (best move from an earlier search of this position)
//   2. winning/equal captures, most valuable victim first
//   3. two killer moves and the countermove (quiet moves that recently caused cutoffs)
//   4. remaining quiet moves, by history score
//   5. losing captures
#include "movepick.h"

namespace bastion {

namespace {
constexpr int MvvValue[7] = {100, 320, 330, 500, 950, 0, 0};
}

MovePicker::MovePicker(const Position& p, Move tt, int /*depth*/, const Worker& worker, const Stack* s)
    : pos(p), w(worker), ss(s) {
    ttMove     = tt && pos.pseudo_legal(tt) ? tt : Move::none();
    killers[0] = ss->killers[0];
    killers[1] = ss->killers[1];
    counter    = Move::none();
    if (!Search::Enabled[Search::Killers]) killers[0] = killers[1] = Move::none();
    else if ((ss - 1)->currentMove.is_ok()) {
        Square prevSq = (ss - 1)->currentMove.to();
        counter       = w.counterMoves[pos.piece_on(prevSq)][prevSq];
    }
    stage = pos.in_check() ? EVASION_TT : MAIN_TT;
    if (!ttMove) ++stage;
}

MovePicker::MovePicker(const Position& p, Move tt, const Worker& worker, const Stack* s)
    : pos(p), w(worker), ss(s) {
    const bool inCheck = pos.in_check();
    ttMove     = tt && pos.pseudo_legal(tt) && (inCheck || pos.is_tactical(tt)) ? tt : Move::none();
    killers[0] = killers[1] = counter = Move::none();
    stage                             = inCheck ? EVASION_TT : QSEARCH_TT;
    if (!ttMove) ++stage;
}

void MovePicker::score_captures() {
    for (ExtMove* m = cur; m < endMoves; ++m) {
        Move      mv       = m->move;
        PieceType captured = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
        Piece     moved    = pos.moved_piece(mv);
        m->value = 16 * MvvValue[captured] + w.captureHistory[moved][mv.to()][captured] / 16;
        if (mv.type() == PROMOTION && mv.promotion_type() == QUEEN) m->value += 16 * 800;
    }
}

void MovePicker::score_quiets() {
    const Color us = pos.side_to_move();
    if (!Search::Enabled[Search::HistoryOrdering]) {
        for (ExtMove* m = cur; m < endMoves; ++m) m->value = 0;
        return;
    }
    for (ExtMove* m = cur; m < endMoves; ++m) {
        Move   mv = m->move;
        Piece  pc = pos.moved_piece(mv);
        Square to = mv.to();
        m->value  = w.mainHistory[us][mv.from()][to] + (*(ss - 1)->contHist)[pc][to] + (*(ss - 2)->contHist)[pc][to] +
                   (*(ss - 4)->contHist)[pc][to] / 2;
    }
}

void MovePicker::score_evasions() {
    const Color us = pos.side_to_move();
    for (ExtMove* m = cur; m < endMoves; ++m) {
        Move mv = m->move;
        if (pos.is_capture(mv)) {
            PieceType captured = mv.type() == EN_PASSANT ? PAWN : type_of(pos.piece_on(mv.to()));
            m->value = (1 << 28) + 16 * MvvValue[captured] - type_of(pos.moved_piece(mv));
        } else {
            Piece pc = pos.moved_piece(mv);
            m->value = w.mainHistory[us][mv.from()][mv.to()] + (*(ss - 1)->contHist)[pc][mv.to()];
        }
    }
}

// Selection step: move the highest scored move to the front of the range.
Move MovePicker::pick_best(ExtMove* begin, ExtMove* end) {
    ExtMove* best = begin;
    for (ExtMove* m = begin + 1; m < end; ++m)
        if (m->value > best->value) best = m;
    std::swap(*begin, *best);
    return begin->move;
}

Move MovePicker::next_move(bool skipQuiets) {
    while (true) {
        switch (stage) {
            case MAIN_TT:
            case EVASION_TT:
            case QSEARCH_TT:
                ++stage;
                return ttMove;

            case CAPTURE_INIT:
            case QCAPTURE_INIT:
                cur = endBadCaptures = moves;
                endMoves              = generate<CAPTURES>(pos, cur);
                score_captures();
                ++stage;
                break;

            case GOOD_CAPTURE:
                while (cur < endMoves) {
                    Move m = pick_best(cur, endMoves);
                    ++cur;
                    if (m == ttMove) continue;
                    if (pos.see_ge(m, 0)) return m;
                    *endBadCaptures++ = cur[-1];  // keep losing captures for the end
                }
                ++stage;
                break;

            case KILLER1:
            case KILLER2: {
                Move k = killers[stage - KILLER1];
                ++stage;
                if (k && k != ttMove && (stage - 1 == KILLER1 || k != killers[0]) && !pos.is_tactical(k) &&
                    pos.pseudo_legal(k))
                    return k;
                break;
            }

            case COUNTERMOVE:
                ++stage;
                if (counter && counter != ttMove && counter != killers[0] && counter != killers[1] &&
                    !pos.is_tactical(counter) && pos.pseudo_legal(counter))
                    return counter;
                break;

            case QUIET_INIT:
                if (!skipQuiets) {
                    cur      = endBadCaptures;
                    endMoves = generate<QUIETS>(pos, cur);
                    score_quiets();
                }
                ++stage;
                break;

            case QUIET:
                if (!skipQuiets)
                    while (cur < endMoves) {
                        Move m = pick_best(cur, endMoves);
                        ++cur;
                        if (!is_special(m)) return m;
                    }
                ++stage;
                cur = moves;  // losing captures were stored at the front
                break;

            case BAD_CAPTURE:
                while (cur < endBadCaptures) {
                    Move m = (cur++)->move;
                    if (m != ttMove) return m;
                }
                stage = DONE;
                break;

            case EVASION_INIT:
                cur      = moves;
                endMoves = generate<EVASIONS>(pos, cur);
                score_evasions();
                ++stage;
                break;

            case EVASION:
            case QCAPTURE:
                while (cur < endMoves) {
                    Move m = pick_best(cur, endMoves);
                    ++cur;
                    if (m != ttMove) return m;
                }
                stage = DONE;
                break;

            case DONE:
            default:
                return Move::none();
        }
    }
}

}  // namespace bastion
