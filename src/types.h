// Bastion - a UCI chess engine
// types.h: core types and small helpers shared by every module.
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace bastion {

using Bitboard = std::uint64_t;
using Key      = std::uint64_t;
using Value    = int;

// Plain ints keep arithmetic on squares/pieces simple and fast.
using Color     = int;
using PieceType = int;
using Piece     = int;
using Square    = int;

constexpr int MAX_PLY   = 128;  // deepest ply the search will ever reach
constexpr int MAX_MOVES = 256;  // upper bound on legal moves in any position

constexpr Color WHITE = 0, BLACK = 1;

constexpr PieceType PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5;
constexpr PieceType NO_PIECE_TYPE = 6;

// Piece encoding: color * 6 + type, so W_PAWN..W_KING = 0..5, B_PAWN..B_KING = 6..11.
constexpr Piece NO_PIECE = 12;

constexpr Square SQ_A1 = 0, SQ_B1 = 1, SQ_C1 = 2, SQ_D1 = 3, SQ_E1 = 4, SQ_F1 = 5, SQ_G1 = 6, SQ_H1 = 7;
constexpr Square SQ_A8 = 56, SQ_B8 = 57, SQ_C8 = 58, SQ_D8 = 59, SQ_E8 = 60, SQ_F8 = 61, SQ_G8 = 62, SQ_H8 = 63;
constexpr Square SQ_NONE = 64;

constexpr int FILE_A = 0, FILE_H = 7;
constexpr int RANK_1 = 0, RANK_2 = 1, RANK_3 = 2, RANK_4 = 3, RANK_5 = 4, RANK_6 = 5, RANK_7 = 6, RANK_8 = 7;

// Scores. Mate scores are expressed relative to the root: mate in N plies = VALUE_MATE - N.
constexpr Value VALUE_ZERO             = 0;
constexpr Value VALUE_DRAW             = 0;
constexpr Value VALUE_MATE             = 32000;
constexpr Value VALUE_INFINITE         = 32001;
constexpr Value VALUE_NONE             = 32002;
constexpr Value VALUE_MATE_IN_MAX_PLY  = VALUE_MATE - MAX_PLY;
constexpr Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;

constexpr Value mate_in(int ply) { return VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }
constexpr bool  is_mate_score(Value v) { return v >= VALUE_MATE_IN_MAX_PLY || v <= VALUE_MATED_IN_MAX_PLY; }

constexpr Piece     make_piece(Color c, PieceType pt) { return c * 6 + pt; }
// type_of(NO_PIECE) is NO_PIECE_TYPE, so tables indexed by piece type can have a zero slot for "nothing".
constexpr PieceType type_of(Piece p) { return p < 6 ? p : p < 12 ? p - 6 : NO_PIECE_TYPE; }
constexpr Color     color_of(Piece p) { return p < 6 ? WHITE : BLACK; }

constexpr int    file_of(Square s) { return s & 7; }
constexpr int    rank_of(Square s) { return s >> 3; }
constexpr Square make_square(int file, int rank) { return rank * 8 + file; }
constexpr bool   is_ok(Square s) { return s >= 0 && s < 64; }
// Mirror a square vertically, so the board is seen from black's side.
constexpr Square flip(Square s) { return s ^ 56; }
constexpr Square relative_square(Color c, Square s) { return c == WHITE ? s : flip(s); }
constexpr int    relative_rank(Color c, Square s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }
constexpr int    pawn_push(Color c) { return c == WHITE ? 8 : -8; }

// Packed middlegame/endgame score: both halves live in one int so they can be
// added together in a single instruction. The endgame half is the low 16 bits.
using Score = int;
constexpr Score S(int mg, int eg) { return int(unsigned(eg) + (unsigned(mg) << 16)); }
constexpr int   mg_value(Score s) { return int16_t(uint16_t((unsigned(s) + 0x8000u) >> 16)); }
constexpr int   eg_value(Score s) { return int16_t(uint16_t(unsigned(s))); }

// ---------------------------------------------------------------------------
// Moves are 16 bits: bits 0-5 origin, 6-11 destination, 12-13 promotion piece
// (knight..queen), 14-15 special move type.
// ---------------------------------------------------------------------------
enum MoveType : std::uint16_t {
    NORMAL     = 0,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14,
};

class Move {
   public:
    constexpr Move() = default;
    constexpr explicit Move(std::uint16_t d) : data(d) {}
    constexpr Move(Square from, Square to) : data(std::uint16_t(from | (to << 6))) {}

    template <MoveType T>
    static constexpr Move make(Square from, Square to, PieceType promo = KNIGHT) {
        return Move(std::uint16_t(T | ((promo - KNIGHT) << 12) | (to << 6) | from));
    }

    static constexpr Move none() { return Move(0); }
    static constexpr Move null() { return Move(65); }

    constexpr Square    from() const { return data & 0x3F; }
    constexpr Square    to() const { return (data >> 6) & 0x3F; }
    constexpr MoveType  type() const { return MoveType(data & (3 << 14)); }
    constexpr PieceType promotion_type() const { return PieceType(((data >> 12) & 3) + KNIGHT); }
    // A move is "ok" when it is neither none() nor null().
    constexpr bool          is_ok() const { return from() != to(); }
    constexpr std::uint16_t raw() const { return data; }

    constexpr bool operator==(const Move& m) const { return data == m.data; }
    constexpr bool operator!=(const Move& m) const { return data != m.data; }
    constexpr explicit operator bool() const { return data != 0; }

   private:
    std::uint16_t data = 0;
};

// Bit helpers built on C++20 <bit>, which compiles to popcnt/tzcnt where available.
inline int    popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) {
    assert(b);
    return std::countr_zero(b);
}
inline Square msb(Bitboard b) {
    assert(b);
    return 63 ^ std::countl_zero(b);
}
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool     more_than_one(Bitboard b) { return b & (b - 1); }
constexpr Bitboard square_bb(Square s) { return Bitboard(1) << s; }

std::string square_name(Square s);
std::string move_to_uci(Move m);

}  // namespace bastion
