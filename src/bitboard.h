// bitboard.h: bitboard constants, precomputed attack tables and magic
// bitboards for sliding pieces.
#pragma once

#include <string>

#include "types.h"

namespace bastion {

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << 8;
constexpr Bitboard Rank3BB = Rank1BB << 16;
constexpr Bitboard Rank4BB = Rank1BB << 24;
constexpr Bitboard Rank5BB = Rank1BB << 32;
constexpr Bitboard Rank6BB = Rank1BB << 40;
constexpr Bitboard Rank7BB = Rank1BB << 48;
constexpr Bitboard Rank8BB = Rank1BB << 56;

constexpr Bitboard DarkSquares = 0xAA55AA55AA55AA55ULL;

constexpr Bitboard file_bb(int f) { return FileABB << f; }
constexpr Bitboard rank_bb(int r) { return Rank1BB << (8 * r); }
constexpr Bitboard file_bb_of(Square s) { return file_bb(file_of(s)); }
constexpr Bitboard rank_bb_of(Square s) { return rank_bb(rank_of(s)); }

// Shift a whole bitboard one step in a direction, dropping bits that would wrap.
constexpr Bitboard shift_north(Bitboard b) { return b << 8; }
constexpr Bitboard shift_south(Bitboard b) { return b >> 8; }
constexpr Bitboard shift_east(Bitboard b) { return (b & ~FileHBB) << 1; }
constexpr Bitboard shift_west(Bitboard b) { return (b & ~FileABB) >> 1; }
constexpr Bitboard shift_up(Color c, Bitboard b) { return c == WHITE ? b << 8 : b >> 8; }
constexpr Bitboard shift_down(Color c, Bitboard b) { return c == WHITE ? b >> 8 : b << 8; }

// All squares attacked by a set of pawns of color c.
constexpr Bitboard pawn_attacks_bb(Color c, Bitboard pawns) {
    return c == WHITE ? ((pawns & ~FileABB) << 7) | ((pawns & ~FileHBB) << 9)
                      : ((pawns & ~FileABB) >> 9) | ((pawns & ~FileHBB) >> 7);
}

constexpr Bitboard adjacent_files_bb(Square s) {
    return shift_east(file_bb_of(s)) | shift_west(file_bb_of(s));
}

// Fill every square in front of the given bits (from c's point of view).
constexpr Bitboard fill_up(Color c, Bitboard b) {
    if (c == WHITE) {
        b |= b << 8;
        b |= b << 16;
        b |= b << 32;
    } else {
        b |= b >> 8;
        b |= b >> 16;
        b |= b >> 32;
    }
    return b;
}

struct Magic {
    Bitboard  mask;
    Bitboard  magic;
    Bitboard* attacks;
    unsigned  shift;

    unsigned index(Bitboard occupied) const { return unsigned(((occupied & mask) * magic) >> shift); }
};

extern Magic    RookMagics[64];
extern Magic    BishopMagics[64];
extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];    // squares strictly between two aligned squares
extern Bitboard LineBB[64][64];       // full line through two aligned squares (0 if not aligned)
extern Bitboard ForwardFileBB[2][64]; // squares in front of s on its file
extern Bitboard PassedPawnMask[2][64];// squares an enemy pawn must not occupy for s to be passed
extern Bitboard PawnAttackSpan[2][64];// squares a pawn on s could ever attack while advancing
extern int      SquareDistance[64][64];

void init_bitboards();

inline Bitboard bishop_attacks(Square s, Bitboard occupied) {
    const Magic& m = BishopMagics[s];
    return m.attacks[m.index(occupied)];
}
inline Bitboard rook_attacks(Square s, Bitboard occupied) {
    const Magic& m = RookMagics[s];
    return m.attacks[m.index(occupied)];
}
inline Bitboard queen_attacks(Square s, Bitboard occupied) {
    return bishop_attacks(s, occupied) | rook_attacks(s, occupied);
}

// Attacks of a non-pawn piece type from square s.
inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
    switch (pt) {
        case KNIGHT: return KnightAttacks[s];
        case BISHOP: return bishop_attacks(s, occupied);
        case ROOK: return rook_attacks(s, occupied);
        case QUEEN: return queen_attacks(s, occupied);
        case KING: return KingAttacks[s];
        default: return 0;
    }
}

inline bool aligned(Square a, Square b, Square c) { return LineBB[a][b] & square_bb(c); }
inline int  distance(Square a, Square b) { return SquareDistance[a][b]; }

std::string bitboard_to_string(Bitboard b);

}  // namespace bastion
