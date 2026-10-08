// tt.cpp: transposition table storage and replacement policy.
#include "tt.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace bastion {

TranspositionTable TT;

namespace {
// High 64 bits of a 64x64-bit product: maps a key uniformly onto [0, n).
#if defined(__SIZEOF_INT128__)
__extension__ typedef unsigned __int128 uint128;
#endif

inline std::uint64_t mul_hi64(std::uint64_t a, std::uint64_t b) {
#if defined(__SIZEOF_INT128__)
    return std::uint64_t((static_cast<uint128>(a) * b) >> 64);
#else
    std::uint64_t aL = std::uint32_t(a), aH = a >> 32, bL = std::uint32_t(b), bH = b >> 32;
    std::uint64_t c1 = (aL * bL) >> 32;
    std::uint64_t c2 = aH * bL + c1;
    std::uint64_t c3 = aL * bH + std::uint32_t(c2);
    return aH * bH + (c2 >> 32) + (c3 >> 32);
#endif
}
}  // namespace

void TTEntry::save(Key k, Value v, bool pv, Bound b, int d, Move m, Value ev, std::uint8_t generation) {
    const std::uint16_t k16 = std::uint16_t(k);
    // Keep an existing best move if this search did not produce one.
    if (m || k16 != key16) move16 = m.raw();
    // Overwrite unless we would replace a deeper result for the same position with a shallow one.
    if (b == BOUND_EXACT || k16 != key16 || d + DepthOffset + 4 > depth8 || (genBound8 & 0xF8) != generation) {
        key16     = k16;
        value16   = std::int16_t(v);
        eval16    = std::int16_t(ev);
        depth8    = std::uint8_t(d + DepthOffset);
        genBound8 = std::uint8_t(generation | (pv ? 4 : 0) | b);
    }
}

TranspositionTable::~TranspositionTable() { delete[] table; }

void TranspositionTable::resize(std::size_t megabytes) {
    delete[] table;
    table        = nullptr;
    clusterCount = std::max<std::size_t>(1, megabytes * 1024 * 1024 / sizeof(Cluster));
    table        = new Cluster[clusterCount];
    clear();
}

void TranspositionTable::clear() {
    if (table) std::memset(static_cast<void*>(table), 0, clusterCount * sizeof(Cluster));
    generation8 = 0;
}

TTEntry* TranspositionTable::first_entry(Key key) const { return &table[mul_hi64(key, clusterCount)].entry[0]; }

void TranspositionTable::prefetch(Key key) const {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(first_entry(key));
#else
    (void)key;
#endif
}

TTEntry* TranspositionTable::probe(Key key, bool& found) const {
    TTEntry*            tte = first_entry(key);
    const std::uint16_t k16 = std::uint16_t(key);

    for (int i = 0; i < ClusterSize; ++i)
        if (tte[i].key16 == k16 || !tte[i].depth8) {
            found = tte[i].depth8 != 0;
            if (found) tte[i].genBound8 = std::uint8_t(generation8 | (tte[i].genBound8 & 7));  // refresh age
            return &tte[i];
        }

    // No match: replace the least valuable entry (shallow and/or from an older search).
    auto worth = [this](const TTEntry& e) {
        int age = ((256 + generation8 - e.genBound8) & 0xF8) / 8;
        return int(e.depth8) - 8 * age;
    };
    TTEntry* replace = tte;
    for (int i = 1; i < ClusterSize; ++i)
        if (worth(tte[i]) < worth(*replace)) replace = &tte[i];
    found = false;
    return replace;
}

int TranspositionTable::hashfull() const {
    int used = 0;
    for (std::size_t i = 0; i < 1000 && i < clusterCount; ++i)
        for (int j = 0; j < ClusterSize; ++j)
            used += table[i].entry[j].depth8 && (table[i].entry[j].genBound8 & 0xF8) == generation8;
    return used * 1000 / (ClusterSize * int(std::min<std::size_t>(1000, clusterCount)));
}

}  // namespace bastion
