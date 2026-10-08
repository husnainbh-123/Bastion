// tt.h: transposition table. A big hash table remembering search results
// (best move, score, depth, bound type) for positions seen before, so
// transpositions are not searched twice and good moves are tried first.
#pragma once

#include <cstddef>
#include <cstdint>

#include "types.h"

namespace bastion {

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    Move  move() const { return Move(move16); }
    Value value() const { return value16; }
    Value eval() const { return eval16; }
    int   depth() const { return int(depth8) - DepthOffset; }
    Bound bound() const { return Bound(genBound8 & 3); }
    bool  is_pv() const { return genBound8 & 4; }

    void save(Key k, Value v, bool pv, Bound b, int d, Move m, Value ev, std::uint8_t generation);

    static constexpr int DepthOffset = 2;  // qsearch stores depth 0; stored depth 0 means "empty"

   private:
    friend class TranspositionTable;
    std::uint16_t key16;
    std::uint16_t move16;
    std::int16_t  value16;
    std::int16_t  eval16;
    std::uint8_t  depth8;
    std::uint8_t  genBound8;  // bits 0-1 bound, bit 2 pv, bits 3-7 generation
};

class TranspositionTable {
   public:
    ~TranspositionTable();
    void         resize(std::size_t megabytes);
    void         clear();
    void         new_search() { generation8 += 8; }
    std::uint8_t generation() const { return generation8; }
    TTEntry*     probe(Key key, bool& found) const;
    int          hashfull() const;  // permille of entries used in the current search
    void         prefetch(Key key) const;

   private:
    static constexpr int ClusterSize = 3;
    struct alignas(32) Cluster {
        TTEntry       entry[ClusterSize];
        std::uint16_t padding;
    };
    static_assert(sizeof(Cluster) == 32, "clusters should be half a cache line");

    TTEntry* first_entry(Key key) const;

    Cluster*     table        = nullptr;
    std::size_t  clusterCount = 0;
    std::uint8_t generation8  = 0;
};

extern TranspositionTable TT;

// Mate scores are stored relative to the node, not the root.
inline Value value_to_tt(Value v, int ply) {
    return v >= VALUE_MATE_IN_MAX_PLY ? v + ply : v <= VALUE_MATED_IN_MAX_PLY ? v - ply : v;
}
inline Value value_from_tt(Value v, int ply) {
    if (v == VALUE_NONE) return VALUE_NONE;
    return v >= VALUE_MATE_IN_MAX_PLY ? v - ply : v <= VALUE_MATED_IN_MAX_PLY ? v + ply : v;
}

}  // namespace bastion
