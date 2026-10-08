// position.h: the board. Holds piece placement as bitboards plus a mailbox,
// game state history (for undo and repetition detection) and Zobrist keys.
#pragma once

#include <string>
#include <vector>

#include "bitboard.h"

namespace bastion {

namespace Zobrist {
extern Key psq[12][64];
extern Key enpassant[8];
extern Key castling[16];
extern Key side;
void       init();
}  // namespace Zobrist

constexpr int WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8;

const std::string StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Everything that cannot be cheaply recomputed when a move is taken back.
struct StateInfo {
    Key    key;
    Key    pawnKey;
    int    castling;
    Square ep;
    int    rule50;
    int    pliesFromNull;

    Piece    captured;
    int      repetition;       // plies back to an identical position (negative = third occurrence)
    Bitboard checkers;         // enemy pieces giving check to the side to move
    Bitboard blockers[2];      // pieces shielding each king from enemy sliders
    Bitboard pinners[2];       // enemy sliders pinning a piece to that king
    Bitboard checkSquares[6];  // where each piece type of the side to move would give check
};

class Position {
   public:
    Position() { set(StartFEN); }

    // Returns false (leaving a valid start position) if the FEN is malformed or illegal.
    bool        set(const std::string& fen, std::string* error = nullptr);
    std::string fen() const;
    std::string pretty() const;

    // ---- Board queries ----
    Piece    piece_on(Square s) const { return board[s]; }
    bool     empty(Square s) const { return board[s] == NO_PIECE; }
    Bitboard occupied() const { return byColor[WHITE] | byColor[BLACK]; }
    Bitboard color_bb(Color c) const { return byColor[c]; }
    Bitboard type_bb(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Bitboard diagonal_sliders(Color c) const { return byColor[c] & (byType[BISHOP] | byType[QUEEN]); }
    Bitboard straight_sliders(Color c) const { return byColor[c] & (byType[ROOK] | byType[QUEEN]); }
    int      count(Color c, PieceType pt) const { return popcount(pieces(c, pt)); }
    Square   king_square(Color c) const { return lsb(pieces(c, KING)); }
    Color    side_to_move() const { return sideToMove; }
    int      game_ply() const { return gamePly; }

    // ---- State queries ----
    const StateInfo& st() const { return states.back(); }
    Key              key() const { return st().key; }
    Key              pawn_key() const { return st().pawnKey; }
    Square           ep_square() const { return st().ep; }
    int              castling_rights() const { return st().castling; }
    int              rule50_count() const { return st().rule50; }
    Bitboard         checkers() const { return st().checkers; }
    bool             in_check() const { return st().checkers != 0; }
    Bitboard         blockers_for_king(Color c) const { return st().blockers[c]; }
    Piece            captured_piece() const { return st().captured; }
    bool has_non_pawn_material(Color c) const { return byColor[c] & ~byType[PAWN] & ~byType[KING]; }

    // ---- Attacks ----
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, occupied()); }
    bool     square_attacked_by(Color c, Square s) const { return attackers_to(s) & byColor[c]; }

    // ---- Move properties ----
    bool  legal(Move m) const;         // assumes m is pseudo-legal
    bool  pseudo_legal(Move m) const;  // validates arbitrary moves (e.g. from the hash table)
    bool  gives_check(Move m) const;
    bool  is_capture(Move m) const { return (!empty(m.to()) && m.type() != CASTLING) || m.type() == EN_PASSANT; }
    bool  is_tactical(Move m) const { return is_capture(m) || m.type() == PROMOTION; }
    Piece moved_piece(Move m) const { return board[m.from()]; }
    // Static exchange evaluation: does the capture sequence on m.to() win at least threshold?
    bool see_ge(Move m, int threshold) const;

    // ---- Making moves ----
    void do_move(Move m);
    void undo_move(Move m);
    void do_null_move();
    void undo_null_move();
    // Approximate key after a move, used to prefetch hash table entries.
    Key key_after(Move m) const;

    // ---- Game state ----
    bool is_repetition_draw(int ply) const { return st().repetition && st().repetition < ply; }
    bool is_insufficient_material() const;
    // Draw by 50-move rule, repetition (2-fold inside the search tree) or dead material.
    bool is_draw(int ply) const;

   private:
    void     put_piece(Piece pc, Square s);
    void     remove_piece(Square s);
    void     move_piece(Square from, Square to);
    void     set_check_info(StateInfo& si) const;
    void     compute_repetition(StateInfo& si) const;
    Bitboard slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const;
    bool     set_internal(const std::string& fen, std::string& error);

    Piece                  board[64];
    Bitboard               byType[6];
    Bitboard               byColor[2];
    Color                  sideToMove;
    int                    gamePly;
    int                    castlingRightsMask[64];
    std::vector<StateInfo> states;
};

}  // namespace bastion
