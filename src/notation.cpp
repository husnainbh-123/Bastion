// notation.cpp: SAN generation and game-over detection.
#include "notation.h"

#include "movegen.h"

namespace bastion {

std::string move_to_san(Position& pos, Move m) {
    std::string san;
    const Square    from = m.from(), to = m.to();
    const PieceType pt   = type_of(pos.moved_piece(m));

    if (m.type() == CASTLING) {
        san = to > from ? "O-O" : "O-O-O";
    } else {
        const bool capture = pos.is_capture(m);
        if (pt == PAWN) {
            if (capture) san = std::string(1, char('a' + file_of(from))) + 'x';
        } else {
            san = "PNBRQK"[pt];
            // Disambiguate between identical pieces that can reach the same square.
            bool ambiguous = false, sameFile = false, sameRank = false;
            for (const ExtMove& other : MoveList<LEGAL>(pos)) {
                Move o = other.move;
                if (o == m || o.to() != to || o.from() == from || type_of(pos.moved_piece(o)) != pt) continue;
                ambiguous = true;
                sameFile |= file_of(o.from()) == file_of(from);
                sameRank |= rank_of(o.from()) == rank_of(from);
            }
            if (ambiguous) {
                if (!sameFile) san += char('a' + file_of(from));
                else if (!sameRank) san += char('1' + rank_of(from));
                else san += square_name(from);
            }
            if (capture) san += 'x';
        }
        san += square_name(to);
        if (m.type() == PROMOTION) {
            san += '=';
            san += "PNBRQK"[m.promotion_type()];
        }
    }

    pos.do_move(m);
    if (pos.in_check()) san += MoveList<LEGAL>(pos).size() ? '+' : '#';
    pos.undo_move(m);
    return san;
}

GameStatus game_status(const Position& pos) {
    GameStatus status;
    if (!MoveList<LEGAL>(pos).size()) {
        if (pos.in_check()) {
            status.result = pos.side_to_move() == WHITE ? "0-1" : "1-0";
            status.reason = "checkmate";
        } else {
            status.result = "1/2-1/2";
            status.reason = "stalemate";
        }
    } else if (pos.st().repetition < 0) {
        status.result = "1/2-1/2";
        status.reason = "threefold repetition";
    } else if (pos.rule50_count() >= 100) {
        status.result = "1/2-1/2";
        status.reason = "fifty-move rule";
    } else if (pos.is_insufficient_material()) {
        status.result = "1/2-1/2";
        status.reason = "insufficient material";
    }
    return status;
}

}  // namespace bastion
