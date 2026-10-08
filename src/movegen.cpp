// movegen.cpp: bitboard move generation. Moves are generated set-wise:
// e.g. all single pawn pushes come from one shift of the pawn bitboard.
#include "movegen.h"

namespace bastion {

namespace {

// Shift a bitboard by a compass direction (in square-index steps).
template <int D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == 8) return b << 8;
    else if constexpr (D == -8) return b >> 8;
    else if constexpr (D == 9) return (b & ~FileHBB) << 9;
    else if constexpr (D == 7) return (b & ~FileABB) << 7;
    else if constexpr (D == -7) return (b & ~FileHBB) >> 7;
    else if constexpr (D == -9) return (b & ~FileABB) >> 9;
    else return 0;
}

template <GenType Type>
ExtMove* make_promotions(ExtMove* list, Square from, Square to) {
    if constexpr (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS)
        *list++ = Move::make<PROMOTION>(from, to, QUEEN);
    if constexpr (Type == QUIETS || Type == EVASIONS || Type == NON_EVASIONS) {
        *list++ = Move::make<PROMOTION>(from, to, KNIGHT);
        *list++ = Move::make<PROMOTION>(from, to, ROOK);
        *list++ = Move::make<PROMOTION>(from, to, BISHOP);
    }
    return list;
}

template <Color Us, GenType Type>
ExtMove* generate_pawn_moves(const Position& pos, ExtMove* list, Bitboard target) {
    constexpr Color    Them     = Us ^ 1;
    constexpr Bitboard Rank7    = Us == WHITE ? Rank7BB : Rank2BB;
    constexpr Bitboard Rank3    = Us == WHITE ? Rank3BB : Rank6BB;
    constexpr int      Up       = Us == WHITE ? 8 : -8;
    constexpr int      UpRight  = Us == WHITE ? 9 : -7;
    constexpr int      UpLeft   = Us == WHITE ? 7 : -9;

    const Bitboard emptySquares = ~pos.occupied();
    const Bitboard enemies      = Type == EVASIONS ? pos.checkers() : pos.color_bb(Them);
    const Bitboard pawnsOn7     = pos.pieces(Us, PAWN) & Rank7;
    const Bitboard pawnsNotOn7  = pos.pieces(Us, PAWN) & ~Rank7;

    // Single and double pushes (promotions handled below)
    if constexpr (Type != CAPTURES) {
        Bitboard b1 = shift<Up>(pawnsNotOn7) & emptySquares;
        Bitboard b2 = shift<Up>(b1 & Rank3) & emptySquares;
        if constexpr (Type == EVASIONS) {  // only blocking squares help
            b1 &= target;
            b2 &= target;
        }
        while (b1) {
            Square to = pop_lsb(b1);
            *list++   = Move(to - Up, to);
        }
        while (b2) {
            Square to = pop_lsb(b2);
            *list++   = Move(to - Up - Up, to);
        }
    }

    // Promotions, with and without capture
    if (pawnsOn7) {
        Bitboard b1 = shift<UpRight>(pawnsOn7) & enemies;
        Bitboard b2 = shift<UpLeft>(pawnsOn7) & enemies;
        Bitboard b3 = shift<Up>(pawnsOn7) & emptySquares;
        if constexpr (Type == EVASIONS) b3 &= target;
        while (b1) {
            Square to = pop_lsb(b1);
            list      = make_promotions<Type>(list, to - UpRight, to);
        }
        while (b2) {
            Square to = pop_lsb(b2);
            list      = make_promotions<Type>(list, to - UpLeft, to);
        }
        while (b3) {
            Square to = pop_lsb(b3);
            list      = make_promotions<Type>(list, to - Up, to);
        }
    }

    // Ordinary captures and en passant
    if constexpr (Type == CAPTURES || Type == EVASIONS || Type == NON_EVASIONS) {
        Bitboard b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<UpLeft>(pawnsNotOn7) & enemies;
        while (b1) {
            Square to = pop_lsb(b1);
            *list++   = Move(to - UpRight, to);
        }
        while (b2) {
            Square to = pop_lsb(b2);
            *list++   = Move(to - UpLeft, to);
        }

        Square ep = pos.ep_square();
        if (ep != SQ_NONE) {
            // An en passant capture cannot resolve a check discovered by the double push.
            if (Type == EVASIONS && (target & square_bb(ep + Up))) return list;
            Bitboard b = pawnsNotOn7 & PawnAttacks[Them][ep];
            while (b) *list++ = Move::make<EN_PASSANT>(pop_lsb(b), ep);
        }
    }
    return list;
}

template <Color Us, PieceType Pt>
ExtMove* generate_piece_moves(const Position& pos, ExtMove* list, Bitboard target) {
    Bitboard pieces = pos.pieces(Us, Pt);
    while (pieces) {
        Square   from = pop_lsb(pieces);
        Bitboard b    = attacks_bb(Pt, from, pos.occupied()) & target;
        while (b) *list++ = Move(from, pop_lsb(b));
    }
    return list;
}

template <Color Us>
ExtMove* generate_castling(const Position& pos, ExtMove* list) {
    constexpr Color  Them = Us ^ 1;
    constexpr int    OO = Us == WHITE ? WHITE_OO : BLACK_OO, OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;
    constexpr Square K = Us == WHITE ? SQ_E1 : SQ_E8;
    const int        cr = pos.castling_rights();

    if ((cr & OO) && pos.empty(K + 1) && pos.empty(K + 2) && !pos.square_attacked_by(Them, K + 1) &&
        !pos.square_attacked_by(Them, K + 2))
        *list++ = Move::make<CASTLING>(K, K + 2);
    if ((cr & OOO) && pos.empty(K - 1) && pos.empty(K - 2) && pos.empty(K - 3) &&
        !pos.square_attacked_by(Them, K - 1) && !pos.square_attacked_by(Them, K - 2))
        *list++ = Move::make<CASTLING>(K, K - 2);
    return list;
}

template <Color Us, GenType Type>
ExtMove* generate_all(const Position& pos, ExtMove* list) {
    const Square ksq = pos.king_square(Us);
    Bitboard     target = 0;

    // In double check only the king can move.
    if (Type != EVASIONS || !more_than_one(pos.checkers())) {
        if constexpr (Type == EVASIONS) target = BetweenBB[ksq][lsb(pos.checkers())] | pos.checkers();
        else if constexpr (Type == NON_EVASIONS) target = ~pos.color_bb(Us);
        else if constexpr (Type == CAPTURES) target = pos.color_bb(Us ^ 1);
        else target = ~pos.occupied();

        list = generate_pawn_moves<Us, Type>(pos, list, target);
        list = generate_piece_moves<Us, KNIGHT>(pos, list, target);
        list = generate_piece_moves<Us, BISHOP>(pos, list, target);
        list = generate_piece_moves<Us, ROOK>(pos, list, target);
        list = generate_piece_moves<Us, QUEEN>(pos, list, target);
    }

    Bitboard kingTarget = Type == EVASIONS ? ~pos.color_bb(Us) : target;
    Bitboard b          = KingAttacks[ksq] & kingTarget;
    while (b) *list++ = Move(ksq, pop_lsb(b));

    if constexpr (Type == QUIETS || Type == NON_EVASIONS)
        if (pos.castling_rights() && !pos.checkers()) list = generate_castling<Us>(pos, list);

    return list;
}

}  // namespace

template <GenType Type>
ExtMove* generate(const Position& pos, ExtMove* list) {
    return pos.side_to_move() == WHITE ? generate_all<WHITE, Type>(pos, list) : generate_all<BLACK, Type>(pos, list);
}

template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<EVASIONS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);

template <>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* list) {
    Color    us     = pos.side_to_move();
    Bitboard pinned = pos.blockers_for_king(us) & pos.color_bb(us);
    Square   ksq    = pos.king_square(us);
    ExtMove* cur    = list;

    list = pos.checkers() ? generate<EVASIONS>(pos, list) : generate<NON_EVASIONS>(pos, list);
    while (cur != list) {
        Move m = cur->move;
        if (((pinned & square_bb(m.from())) || m.from() == ksq || m.type() == EN_PASSANT) && !pos.legal(m))
            *cur = *(--list);
        else
            ++cur;
    }
    return list;
}

}  // namespace bastion
