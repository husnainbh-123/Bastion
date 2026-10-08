// perft_tests.cpp: move generator correctness suite.
//
// Perft counts every leaf node of the legal move tree to a fixed depth. The
// expected numbers below are well-known reference values; any bug in castling,
// en passant, promotions, pins or check evasion changes at least one of them.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/movegen.h"
#include "../src/position.h"

using namespace bastion;

namespace {

std::uint64_t perft(Position& pos, int depth) {
    MoveList<LEGAL> moves(pos);
    if (depth == 1) return moves.size();
    std::uint64_t nodes = 0;
    for (const ExtMove& m : moves) {
        pos.do_move(m);
        nodes += perft(pos, depth - 1);
        pos.undo_move(m);
    }
    return nodes;
}

struct PerftCase {
    const char*   fen;
    int           depth;
    std::uint64_t nodes;
};

// Fast suite: runs in a few seconds and is executed on every CI build.
const std::vector<PerftCase> FastSuite = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
    {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4, 422333},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
    // Edge cases: illegal en passant, castling rules, promotions, discovered checks, stalemate.
    {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888},
    {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133},
    {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467},
    {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072},
    {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711},
    {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206},
    {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476},
    {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001},
    {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658},
    {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342},
    {"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683},
    {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217},
    {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584},
    {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527},
};

// Deep suite: hundreds of millions of nodes, run with --deep.
const std::vector<PerftCase> DeepSuite = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15833292},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551},
};

}  // namespace

int main(int argc, char** argv) {
    bool deep = argc > 1 && std::string(argv[1]) == "--deep";
    const auto& suite = deep ? DeepSuite : FastSuite;

    int           failures = 0;
    std::uint64_t total    = 0;
    auto          start    = std::chrono::steady_clock::now();

    for (const PerftCase& c : suite) {
        Position    pos;
        std::string error;
        if (!pos.set(c.fen, &error)) {
            std::printf("FAIL  could not parse %s (%s)\n", c.fen, error.c_str());
            ++failures;
            continue;
        }
        std::uint64_t nodes = perft(pos, c.depth);
        total += nodes;
        bool ok = nodes == c.nodes;
        failures += !ok;
        std::printf("%s  depth %d  %12llu  %s\n", ok ? "ok  " : "FAIL", c.depth, (unsigned long long)nodes, c.fen);
        if (!ok) std::printf("      expected %llu\n", (unsigned long long)c.nodes);
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("\n%zu positions, %llu nodes, %.2fs (%.1f Mnps), %d failure(s)\n", suite.size(),
                (unsigned long long)total, secs, total / secs / 1e6, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
