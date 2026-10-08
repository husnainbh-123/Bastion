// notation.h: Standard Algebraic Notation (SAN), as used in scoresheets and PGN.
#pragma once

#include <string>

#include "position.h"

namespace bastion {

// SAN for a legal move in the given position, e.g. "Nbd7", "exd6", "O-O", "e8=Q#".
std::string move_to_san(Position& pos, Move m);

// Game-over state of a position: result is "1-0", "0-1", "1/2-1/2" or "*".
struct GameStatus {
    std::string result = "*";
    std::string reason;  // "checkmate", "stalemate", "threefold repetition", ...
};
GameStatus game_status(const Position& pos);

}  // namespace bastion
