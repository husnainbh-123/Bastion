// movegen.h: pseudo-legal and legal move generation.
#pragma once

#include <algorithm>

#include "position.h"

namespace bastion {

enum GenType {
    CAPTURES,      // captures and queen promotions
    QUIETS,        // non-captures, castling and under-promotions
    EVASIONS,      // all moves that might get out of check
    NON_EVASIONS,  // all pseudo-legal moves when not in check
    LEGAL,         // fully legal moves
};

struct ExtMove {
    Move move;
    int  value;

    operator Move() const { return move; }
    void operator=(Move m) { move = m; }
};

inline bool operator<(const ExtMove& a, const ExtMove& b) { return a.value < b.value; }

template <GenType T>
ExtMove* generate(const Position& pos, ExtMove* list);

template <GenType T>
struct MoveList {
    explicit MoveList(const Position& pos) : last(generate<T>(pos, moves)) {}
    const ExtMove* begin() const { return moves; }
    const ExtMove* end() const { return last; }
    std::size_t    size() const { return std::size_t(last - moves); }
    bool           contains(Move m) const {
        return std::any_of(begin(), end(), [m](const ExtMove& e) { return e.move == m; });
    }

   private:
    ExtMove moves[MAX_MOVES], *last;
};

}  // namespace bastion
