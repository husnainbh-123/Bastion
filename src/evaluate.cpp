// evaluate.cpp: hand-crafted evaluation.
//
// The score is built from packed (middlegame, endgame) terms which are blended
// according to the amount of material left on the board. All weights live in
// Eval::P and are tuned automatically (see tools/tuner) on positions from
// self-play games; the evaluation code itself only describes *what* to look
// at, not how much it is worth.
#include "evaluate.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "eval_params.h"

namespace bastion::Eval {

Params P;

namespace {

template <bool Tracing>
class Evaluator {
   public:
    Evaluator(const Position& p, Trace* t) : pos(p), trace(t) {}
    Value value();  // white's point of view

    int phase = 0, scale = 128;

   private:
    void add(Color c, const Score& param, int count = 1) {
        score += (c == WHITE ? param : -param) * count;
        if constexpr (Tracing) trace->coeff[&param - reinterpret_cast<const Score*>(&P)][c] += count;
    }

    template <Color Us> void initialize();
    template <Color Us, PieceType Pt> void pieces();
    template <Color Us> void pawns();
    template <Color Us> void king();
    template <Color Us> void threats();
    template <Color Us> void passed();
    int  scale_factor(int eg) const;

    const Position& pos;
    Trace*          trace;
    Score           score = 0;

    Bitboard attackedBy[2][7] = {};  // [color][piece type], index 6 = any piece
    Bitboard attackedBy2[2]   = {};  // squares attacked at least twice
    Bitboard mobilityArea[2]  = {};
    Bitboard kingZone[2]      = {};
    Bitboard passedPawns[2]   = {};
    Bitboard pawnSpan[2]      = {};  // squares the side's pawns could ever attack
    int      kingAttackers[2] = {};  // enemy pieces attacking this side's king zone
};

constexpr int ALL = 6;

template <bool Tracing>
template <Color Us>
void Evaluator<Tracing>::initialize() {
    constexpr Color Them = Us ^ 1;
    const Square    ksq  = pos.king_square(Us);
    const Bitboard  pawns = pos.pieces(Us, PAWN);

    attackedBy[Us][PAWN] = pawn_attacks_bb(Us, pawns);
    attackedBy[Us][KING] = KingAttacks[ksq];
    attackedBy[Us][ALL]  = attackedBy[Us][PAWN] | attackedBy[Us][KING];
    attackedBy2[Us]      = (attackedBy[Us][PAWN] & attackedBy[Us][KING]) |
                      (shift_east(shift_up(Us, pawns)) & shift_west(shift_up(Us, pawns)));
    pawnSpan[Us] = fill_up(Us, attackedBy[Us][PAWN]);

    // Squares a piece can usefully move to: not our pawns or king, not attacked by enemy pawns.
    mobilityArea[Us] = ~(pawns | pos.pieces(Us, KING) | pawn_attacks_bb(Them, pos.pieces(Them, PAWN)));

    Bitboard zone = KingAttacks[ksq] | square_bb(ksq);
    kingZone[Us]  = zone | shift_up(Us, zone);
}

template <bool Tracing>
template <Color Us, PieceType Pt>
void Evaluator<Tracing>::pieces() {
    constexpr Color Them = Us ^ 1;
    const Bitboard  occ  = pos.occupied();
    Bitboard        bb   = pos.pieces(Us, Pt);

    while (bb) {
        const Square s = pop_lsb(bb);
        Bitboard     att;
        // Sliders see through friendly pieces that move along the same lines (batteries).
        if constexpr (Pt == BISHOP) att = bishop_attacks(s, occ ^ pos.pieces(Us, QUEEN));
        else if constexpr (Pt == ROOK) att = rook_attacks(s, occ ^ pos.pieces(Us, QUEEN) ^ pos.pieces(Us, ROOK));
        else att = attacks_bb(Pt, s, occ);

        // A pinned piece only controls squares on the pin line.
        if (pos.blockers_for_king(Us) & square_bb(s)) att &= LineBB[pos.king_square(Us)][s];

        attackedBy2[Us] |= attackedBy[Us][ALL] & att;
        attackedBy[Us][Pt] |= att;
        attackedBy[Us][ALL] |= att;

        const int mob = popcount(att & mobilityArea[Us]);
        if constexpr (Pt == KNIGHT) add(Us, P.knightMobility[mob]);
        if constexpr (Pt == BISHOP) add(Us, P.bishopMobility[mob]);
        if constexpr (Pt == ROOK) add(Us, P.rookMobility[mob]);
        if constexpr (Pt == QUEEN) add(Us, P.queenMobility[mob]);

        if (Bitboard zoneAttacks = att & kingZone[Them]) {
            kingAttackers[Them]++;
            add(Us, P.kingAttackWeight[Pt], popcount(zoneAttacks));
        }

        if constexpr (Pt == KNIGHT || Pt == BISHOP) {
            // Outpost: in enemy territory, defended by a pawn, never attackable by enemy pawns.
            constexpr Bitboard Territory = Us == WHITE ? (Rank4BB | Rank5BB | Rank6BB) : (Rank5BB | Rank4BB | Rank3BB);
            if (Territory & attackedBy[Us][PAWN] & ~pawnSpan[Them] & square_bb(s))
                add(Us, Pt == KNIGHT ? P.knightOutpost : P.bishopOutpost);
        }
        if constexpr (Pt == BISHOP) {
            Bitboard sameColor = (DarkSquares & square_bb(s)) ? DarkSquares : ~DarkSquares;
            add(Us, P.bishopPawnsSameColor, popcount(pos.pieces(Us, PAWN) & sameColor));
        }
        if constexpr (Pt == ROOK) {
            if (!(pos.pieces(Us, PAWN) & file_bb_of(s)))
                add(Us, (pos.pieces(Them, PAWN) & file_bb_of(s)) ? P.rookSemiOpenFile : P.rookOpenFile);
            if (relative_rank(Us, s) == RANK_7 &&
                ((pos.pieces(Them, PAWN) & rank_bb_of(s)) || relative_rank(Us, pos.king_square(Them)) == RANK_8))
                add(Us, P.rookOnSeventh);
        }
    }
}

template <bool Tracing>
template <Color Us>
void Evaluator<Tracing>::pawns() {
    constexpr Color Them   = Us ^ 1;
    const Bitboard  ours   = pos.pieces(Us, PAWN);
    const Bitboard  theirs = pos.pieces(Them, PAWN);
    Bitboard        b      = ours;

    while (b) {
        const Square s     = pop_lsb(b);
        const int    r     = relative_rank(Us, s);
        const Square stop  = s + pawn_push(Us);
        const Bitboard adj = adjacent_files_bb(s);

        const bool doubled   = ForwardFileBB[Us][s] & ours;
        const bool isolated  = !(adj & ours);
        const bool supported = PawnAttacks[Them][s] & ours;
        const bool phalanx   = adj & rank_bb_of(s) & ours;
        // Backward: no friendly pawn can ever defend it, and its advance is controlled by an enemy pawn.
        const bool backward = !(ours & PawnAttackSpan[Them][stop]) && (PawnAttacks[Us][stop] & theirs);

        if (doubled) add(Us, P.doubledPawn);
        if (isolated) add(Us, P.isolatedPawn);
        else if (backward) add(Us, P.backwardPawn);
        if (supported || phalanx) add(Us, P.connectedPawn[r]);

        if (!(PassedPawnMask[Us][s] & theirs) && !doubled) passedPawns[Us] |= square_bb(s);
    }
}

template <bool Tracing>
template <Color Us>
void Evaluator<Tracing>::king() {
    constexpr Color Them   = Us ^ 1;
    const Square    ksq    = pos.king_square(Us);
    const Bitboard  ours   = pos.pieces(Us, PAWN);
    const Bitboard  theirs = pos.pieces(Them, PAWN);
    const Bitboard  occ    = pos.occupied();

    // Pawn shelter and storm on the king's file and its neighbours.
    const Bitboard inFront = fill_up(Us, square_bb(ksq)) | rank_bb_of(ksq);
    const int      center  = std::clamp(file_of(ksq), 1, 6);
    for (int f = center - 1; f <= center + 1; ++f) {
        Bitboard mine = ours & file_bb(f) & inFront;
        Bitboard opp  = theirs & file_bb(f) & inFront;
        int      ourRank = mine ? relative_rank(Us, Us == WHITE ? lsb(mine) : msb(mine)) : 0;
        int      oppRank = opp ? relative_rank(Us, Us == WHITE ? lsb(opp) : msb(opp)) : 0;
        add(Us, P.kingShelter[ourRank]);
        add(Us, P.kingStorm[oppRank]);
    }
    if (!(ours & file_bb_of(ksq))) add(Us, P.kingOpenFile);

    // Attack pressure (from the attacker's point of view).
    add(Them, P.kingAttackers[std::min(kingAttackers[Us], 7)]);

    // Safe checks: squares from which an enemy piece can check without being captured.
    const Bitboard safe       = ~pos.color_bb(Them) & ~attackedBy[Us][ALL];
    const Bitboard diagonal   = bishop_attacks(ksq, occ);
    const Bitboard orthogonal = rook_attacks(ksq, occ);
    add(Them, P.safeCheck[KNIGHT], popcount(KnightAttacks[ksq] & attackedBy[Them][KNIGHT] & safe));
    add(Them, P.safeCheck[BISHOP], popcount(diagonal & attackedBy[Them][BISHOP] & safe));
    add(Them, P.safeCheck[ROOK], popcount(orthogonal & attackedBy[Them][ROOK] & safe));
    add(Them, P.safeCheck[QUEEN], popcount((diagonal | orthogonal) & attackedBy[Them][QUEEN] & safe));
}

template <bool Tracing>
template <Color Us>
void Evaluator<Tracing>::threats() {
    constexpr Color Them      = Us ^ 1;
    const Bitboard  targets   = pos.color_bb(Them) & ~pos.type_bb(KING);
    const Bitboard  nonPawns  = targets & ~pos.type_bb(PAWN);

    Bitboard b = attackedBy[Us][PAWN] & nonPawns;
    while (b) add(Us, P.threatByPawn[type_of(pos.piece_on(pop_lsb(b)))]);

    b = (attackedBy[Us][KNIGHT] | attackedBy[Us][BISHOP]) & targets;
    while (b) add(Us, P.threatByMinor[type_of(pos.piece_on(pop_lsb(b)))]);

    b = attackedBy[Us][ROOK] & targets;
    while (b) add(Us, P.threatByRook[type_of(pos.piece_on(pop_lsb(b)))]);

    if (attackedBy[Us][KING] & targets & ~attackedBy[Them][ALL]) add(Us, P.threatByKing);

    add(Us, P.hangingPiece, popcount(targets & attackedBy[Us][ALL] & ~attackedBy[Them][ALL]));
}

template <bool Tracing>
template <Color Us>
void Evaluator<Tracing>::passed() {
    constexpr Color Them = Us ^ 1;
    Bitboard        b    = passedPawns[Us];
    while (b) {
        const Square s    = pop_lsb(b);
        const int    r    = relative_rank(Us, s);
        const Square stop = s + pawn_push(Us);

        add(Us, P.passedPawn[r]);
        add(Us, P.passedOwnKingDistance[distance(pos.king_square(Us), stop)]);
        add(Us, P.passedEnemyKingDistance[distance(pos.king_square(Them), stop)]);

        const Bitboard path = ForwardFileBB[Us][s];
        if (!(path & pos.occupied()) && !(path & attackedBy[Them][ALL])) add(Us, P.passedFreePath[r]);
    }
}

// Endgames that are hard to win get their endgame score scaled toward zero (128 = no change).
template <bool Tracing>
int Evaluator<Tracing>::scale_factor(int eg) const {
    const Color strong = eg > 0 ? WHITE : BLACK, weak = strong ^ 1;
    auto        npm    = [&](Color c) {
        return 3 * pos.count(c, KNIGHT) + 3 * pos.count(c, BISHOP) + 5 * pos.count(c, ROOK) + 9 * pos.count(c, QUEEN);
    };
    const int strongNpm = npm(strong), weakNpm = npm(weak);

    // Without pawns, being up less than a rook is usually not enough to win.
    if (!pos.pieces(strong, PAWN) && strongNpm - weakNpm <= 3) return strongNpm <= 3 ? 0 : 16;

    // Opposite-coloured bishops are notoriously drawish.
    if (pos.count(WHITE, BISHOP) == 1 && pos.count(BLACK, BISHOP) == 1) {
        Bitboard bishops = pos.type_bb(BISHOP);
        if ((bishops & DarkSquares) && (bishops & ~DarkSquares))
            return (strongNpm == 3 && weakNpm == 3) ? 64 : 100;
    }
    return 128;
}

template <bool Tracing>
Value Evaluator<Tracing>::value() {
    initialize<WHITE>();
    initialize<BLACK>();

    // Material and piece-square tables
    for (Color c : {WHITE, BLACK})
        for (PieceType pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(c, pt);
            while (b) {
                Square s = pop_lsb(b);
                add(c, P.pieceValue[pt]);
                add(c, P.psqt[pt][relative_square(c, s)]);
            }
        }

    pawns<WHITE>();
    pawns<BLACK>();
    pieces<WHITE, KNIGHT>();
    pieces<BLACK, KNIGHT>();
    pieces<WHITE, BISHOP>();
    pieces<BLACK, BISHOP>();
    pieces<WHITE, ROOK>();
    pieces<BLACK, ROOK>();
    pieces<WHITE, QUEEN>();
    pieces<BLACK, QUEEN>();
    king<WHITE>();
    king<BLACK>();
    threats<WHITE>();
    threats<BLACK>();
    passed<WHITE>();
    passed<BLACK>();

    if (pos.count(WHITE, BISHOP) >= 2) add(WHITE, P.bishopPair);
    if (pos.count(BLACK, BISHOP) >= 2) add(BLACK, P.bishopPair);

    add(pos.side_to_move(), P.tempo);

    // Mop-up: with a decisive advantage and no pawns left, drive the lone king to the edge.
    Score extra = 0;
    int   mg = mg_value(score), eg = eg_value(score);
    if (!pos.type_bb(PAWN) && std::abs(eg) > 400) {
        Color  strong = eg > 0 ? WHITE : BLACK;
        Square weakK  = pos.king_square(strong ^ 1), strongK = pos.king_square(strong);
        int    centerDist = std::max(3 - file_of(weakK), file_of(weakK) - 4) + std::max(3 - rank_of(weakK), rank_of(weakK) - 4);
        int    bonus      = 10 * centerDist + 4 * (14 - (std::abs(file_of(weakK) - file_of(strongK)) +
                                                    std::abs(rank_of(weakK) - rank_of(strongK))));
        extra = S(0, strong == WHITE ? bonus : -bonus);
    }
    eg += eg_value(extra);

    phase = std::min(24, pos.count(WHITE, KNIGHT) + pos.count(BLACK, KNIGHT) + pos.count(WHITE, BISHOP) +
                             pos.count(BLACK, BISHOP) + 2 * (pos.count(WHITE, ROOK) + pos.count(BLACK, ROOK)) +
                             4 * (pos.count(WHITE, QUEEN) + pos.count(BLACK, QUEEN)));
    scale = scale_factor(eg);

    if constexpr (Tracing) {
        trace->phase = phase;
        trace->scale = scale;
        trace->extra = extra;
    }

    return (mg * phase + eg * scale / 128 * (24 - phase)) / 24;
}

}  // namespace

void init() { set_default_params(P); }

Value evaluate(const Position& pos) {
    Evaluator<false> ev(pos, nullptr);
    Value            v = ev.value();
    v = pos.side_to_move() == WHITE ? v : -v;
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

Value evaluate_trace(const Position& pos, Trace& trace) {
    std::memset(&trace, 0, sizeof(trace));
    Evaluator<true> ev(pos, &trace);
    return ev.value();
}

std::string describe(const Position& pos) {
    Trace tr;
    Value white = evaluate_trace(pos, tr);
    std::ostringstream out;
    out << "Phase " << tr.phase << "/24, endgame scale " << tr.scale << "/128\n";
    out << "Static evaluation: " << std::showpos << white << " cp (white's point of view), " << evaluate(pos)
        << " cp (side to move)\n";
    return out.str();
}

}  // namespace bastion::Eval
