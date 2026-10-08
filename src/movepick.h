// movepick.h: hands out moves one at a time in a promising order, generating
// them in stages so that a cutoff on an early move saves the rest of the work.
#pragma once

#include "movegen.h"
#include "search.h"

namespace bastion {

class MovePicker {
   public:
    // Main search
    MovePicker(const Position& pos, Move ttMove, int depth, const Worker& w, const Stack* ss);
    // Quiescence search: captures only (or all evasions when in check)
    MovePicker(const Position& pos, Move ttMove, const Worker& w, const Stack* ss);

    Move next_move(bool skipQuiets = false);

   private:
    enum Stage {
        MAIN_TT, CAPTURE_INIT, GOOD_CAPTURE, KILLER1, KILLER2, COUNTERMOVE, QUIET_INIT, QUIET, BAD_CAPTURE,
        EVASION_TT, EVASION_INIT, EVASION,
        QSEARCH_TT, QCAPTURE_INIT, QCAPTURE,
        DONE
    };

    void score_captures();
    void score_quiets();
    void score_evasions();
    Move pick_best(ExtMove* begin, ExtMove* end);
    bool is_special(Move m) const { return m == ttMove || m == killers[0] || m == killers[1] || m == counter; }

    const Position& pos;
    const Worker&   w;
    const Stack*    ss;
    Move            ttMove, killers[2], counter;
    int             stage;
    ExtMove*        cur;
    ExtMove*        endMoves;
    ExtMove*        endBadCaptures;
    ExtMove         moves[MAX_MOVES];
};

}  // namespace bastion
