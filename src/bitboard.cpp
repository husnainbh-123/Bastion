// bitboard.cpp: builds attack tables at startup, including "fancy" magic
// bitboards for rooks and bishops. Magic numbers are found by a seeded random
// search, which takes a few milliseconds and is fully deterministic.
#include "bitboard.h"

#include <algorithm>

namespace bastion {

Magic    RookMagics[64];
Magic    BishopMagics[64];
Bitboard PawnAttacks[2][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
Bitboard ForwardFileBB[2][64];
Bitboard PassedPawnMask[2][64];
Bitboard PawnAttackSpan[2][64];
int      SquareDistance[64][64];

namespace {

Bitboard RookTable[0x19000];   // 102400 entries: total of 2^bits over all squares
Bitboard BishopTable[0x1480];  // 5248 entries

// xorshift64* pseudo random generator (fixed seed => same magics every run).
class PRNG {
   public:
    explicit PRNG(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    // Magics with few set bits are found much faster.
    std::uint64_t sparse() { return next() & next() & next(); }

   private:
    std::uint64_t s;
};

// Slow reference implementation, only used while building the tables.
Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {
    static const int RookDirs[4][2]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    const auto&      dirs             = pt == ROOK ? RookDirs : BishopDirs;

    Bitboard attacks = 0;
    for (const auto& d : dirs) {
        int f = file_of(sq) + d[0], r = rank_of(sq) + d[1];
        while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
            Square s = make_square(f, r);
            attacks |= square_bb(s);
            if (occupied & square_bb(s)) break;
            f += d[0];
            r += d[1];
        }
    }
    return attacks;
}

void init_magics(PieceType pt, Bitboard table[], Magic magics[]) {
    static Bitboard occupancy[4096], reference[4096];
    static int      epoch[4096];
    int             attempt = 0, size = 0;
    PRNG            rng(pt == ROOK ? 0x2545F4914F6CDD1DULL : 0x9E3779B97F4A7C15ULL);

    std::fill(std::begin(epoch), std::end(epoch), 0);

    for (Square s = 0; s < 64; ++s) {
        // Edge squares never block anything further, so they are left out of the mask.
        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb_of(s)) | ((FileABB | FileHBB) & ~file_bb_of(s));

        Magic& m  = magics[s];
        m.mask    = sliding_attack(pt, s, 0) & ~edges;
        m.shift   = 64 - popcount(m.mask);
        m.attacks = s == 0 ? table : magics[s - 1].attacks + size;

        // Enumerate every subset of the mask (Carry-Rippler trick).
        Bitboard b = 0;
        size       = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(pt, s, b);
            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        // Try random candidates until one maps every subset without a harmful collision.
        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;) m.magic = rng.sparse();

            for (++attempt, i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < attempt) {
                    epoch[idx]     = attempt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i])
                    break;
            }
        }
    }
}

}  // namespace

void init_bitboards() {
    for (Square a = 0; a < 64; ++a)
        for (Square b = 0; b < 64; ++b)
            SquareDistance[a][b] = std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));

    for (Square s = 0; s < 64; ++s) {
        Bitboard b       = square_bb(s);
        PawnAttacks[WHITE][s] = pawn_attacks_bb(WHITE, b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb(BLACK, b);

        KnightAttacks[s] = 0;
        KingAttacks[s]   = 0;
        static const int KnightSteps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        static const int KingSteps[8][2]   = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        for (const auto& d : KnightSteps) {
            int f = file_of(s) + d[0], r = rank_of(s) + d[1];
            if (f >= 0 && f <= 7 && r >= 0 && r <= 7) KnightAttacks[s] |= square_bb(make_square(f, r));
        }
        for (const auto& d : KingSteps) {
            int f = file_of(s) + d[0], r = rank_of(s) + d[1];
            if (f >= 0 && f <= 7 && r >= 0 && r <= 7) KingAttacks[s] |= square_bb(make_square(f, r));
        }
    }

    init_magics(ROOK, RookTable, RookMagics);
    init_magics(BISHOP, BishopTable, BishopMagics);

    for (Square a = 0; a < 64; ++a)
        for (Square b = 0; b < 64; ++b) {
            BetweenBB[a][b] = LineBB[a][b] = 0;
            if (a == b) continue;
            for (PieceType pt : {BISHOP, ROOK}) {
                if (attacks_bb(pt, a, 0) & square_bb(b)) {
                    LineBB[a][b]    = (attacks_bb(pt, a, 0) & attacks_bb(pt, b, 0)) | square_bb(a) | square_bb(b);
                    BetweenBB[a][b] = attacks_bb(pt, a, square_bb(b)) & attacks_bb(pt, b, square_bb(a));
                }
            }
        }

    for (Color c : {WHITE, BLACK})
        for (Square s = 0; s < 64; ++s) {
            ForwardFileBB[c][s]  = fill_up(c, shift_up(c, square_bb(s)));
            PawnAttackSpan[c][s] = fill_up(c, shift_up(c, adjacent_files_bb(s) & rank_bb_of(s)));
            PassedPawnMask[c][s] = ForwardFileBB[c][s] | PawnAttackSpan[c][s];
        }
}

std::string bitboard_to_string(Bitboard b) {
    std::string out = "+---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f <= 7; ++f) out += (b & square_bb(make_square(f, r))) ? "| X " : "|   ";
        out += "| " + std::to_string(r + 1) + "\n+---+---+---+---+---+---+---+---+\n";
    }
    return out + "  a   b   c   d   e   f   g   h\n";
}

std::string square_name(Square s) {
    if (s == SQ_NONE) return "-";
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

std::string move_to_uci(Move m) {
    if (m == Move::none()) return "0000";
    if (m == Move::null()) return "0000";
    std::string s = square_name(m.from()) + square_name(m.to());
    if (m.type() == PROMOTION) s += "nbrq"[m.promotion_type() - KNIGHT];
    return s;
}

}  // namespace bastion
