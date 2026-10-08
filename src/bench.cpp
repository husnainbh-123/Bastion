// bench.cpp: positions searched by the "bench" command. A mix of openings,
// middlegames and endgames so that every part of the search is exercised.
#include <string>
#include <vector>

namespace bastion {

extern const std::vector<std::string> BenchPositions = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pp2pppp/2np1n2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - 2 6",
    "r1bqkbnr/1ppp1ppp/p1n5/4p3/B3P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 1 4",
    "rnbqkb1r/ppp2ppp/4pn2/3p2B1/2PP4/2N5/PP2PPPP/R2QKBNR b KQkq - 3 4",
    "rnbq1rk1/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP3PPP/R1BQKB1R w KQ - 1 6",
    "rnbqkbnr/ppp2ppp/4p3/3pP3/3P4/8/PPP2PPP/RNBQKBNR b KQkq - 0 3",
    "r1b2rk1/2q1bppp/p2p1n2/np2p3/3PP3/5N1P/PPBN1PP1/R1BQR1K1 w - - 1 13",
    "2r3k1/pp3ppp/2n1b3/3pP3/3P4/P1N2N2/1P3PPP/2R3K1 w - - 0 20",
    "3r1rk1/p4ppp/2p1b3/2Pp4/1P1Pn3/P3P2P/4BPP1/R1R3K1 w - - 0 21",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "1r1qr1k1/1p2bppp/p2pbn2/4p3/4P3/1NN1BP2/PPPQ2PP/2KR1B1R w - - 4 13",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R w KQ - 0 9",
    "r2qr1k1/1b1nbppp/p2p1n2/1pp1p3/4P3/2PP1N1P/PPBN1PP1/R1BQR1K1 w - - 0 13",
    "2kr3r/ppp2ppp/2n1bn2/2b1p3/4P3/2N1BN2/PPP1BPPP/R4RK1 w - - 4 10",
    "r1bqk2r/ppp2ppp/2n5/3np3/1b6/2NP1N2/PP2PPPP/R1BQKB1R w KQkq - 2 7",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "8/5pk1/6p1/8/8/6P1/5PK1/8 w - - 0 1",
    "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1",
    "8/8/8/4k3/8/3K4/3P4/8 w - - 0 1",
    "8/3k4/1p6/1P6/1K6/8/8/8 w - - 0 1",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
    "8/8/1p1k2p1/p1prp2p/P2n3P/6P1/1P1R1PK1/4R3 b - - 5 49",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "4k3/8/8/8/8/8/4P3/R3K2R w KQ - 0 1",
    "8/8/3k4/8/8/4K3/8/7R w - - 0 1",
    "8/1p4kp/p1p3p1/2P1r3/1P6/P5P1/5P1P/3R2K1 w - - 0 30",
    "r5k1/5ppp/1p6/2p5/2P5/1P4P1/P4P1P/R5K1 w - - 0 25",
    "2q3k1/5pp1/p6p/1p6/3Q4/P6P/1P3PP1/6K1 w - - 0 30",
    "8/6pk/5p1p/8/1R6/6PP/r4P1K/8 b - - 0 40",
    "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
};

}  // namespace bastion
