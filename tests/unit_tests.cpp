// unit_tests.cpp: consistency checks for the board code that perft alone
// cannot catch (incremental hashing, check detection, move validation, SEE,
// draw rules and FEN robustness).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/movegen.h"
#include "../src/position.h"

using namespace bastion;

namespace {

int failures = 0;

#define CHECK(cond, msg)                                                              \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++failures;                                                               \
            std::printf("FAIL %s:%d  %s  [%s]\n", __FILE__, __LINE__, #cond, (msg)); \
        }                                                                             \
    } while (0)

// Walk the move tree and check invariants at every node.
void walk(Position& pos, int depth) {
    const std::string fenBefore = pos.fen();
    const Key         keyBefore = pos.key();

    // The incrementally updated key must match one computed from scratch.
    Position fresh;
    fresh.set(fenBefore);
    CHECK(fresh.key() == pos.key(), fenBefore.c_str());
    CHECK(fresh.pawn_key() == pos.pawn_key(), fenBefore.c_str());

    MoveList<LEGAL> legal(pos);

    // Every 16-bit move is pseudo_legal()+legal() exactly when it is in the legal list.
    if (depth >= 2) {
        int count = 0;
        for (unsigned raw = 0; raw < 65536; ++raw) {
            Move m(static_cast<std::uint16_t>(raw));
            bool ok = pos.pseudo_legal(m) && pos.legal(m);
            count += ok;
            if (ok != legal.contains(m)) {
                CHECK(false, (fenBefore + " move " + move_to_uci(m)).c_str());
                break;
            }
        }
        CHECK(count == int(legal.size()), fenBefore.c_str());
    }

    if (depth == 0) return;
    for (const ExtMove& em : legal) {
        Move m      = em.move;
        bool checks = pos.gives_check(m);
        pos.do_move(m);
        CHECK(checks == pos.in_check(), (fenBefore + " " + move_to_uci(m)).c_str());
        walk(pos, depth - 1);
        pos.undo_move(m);
        CHECK(pos.fen() == fenBefore, (fenBefore + " undo " + move_to_uci(m)).c_str());
        CHECK(pos.key() == keyBefore, fenBefore.c_str());
    }
}

Move find_move(const Position& pos, const std::string& uci) {
    for (const ExtMove& m : MoveList<LEGAL>(pos))
        if (move_to_uci(m) == uci) return m;
    return Move::none();
}

void test_move_tree() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1",
        "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",
    };
    for (const char* fen : fens) {
        Position pos;
        pos.set(fen);
        walk(pos, 3);
    }
}

void test_see() {
    struct SeeCase {
        const char* fen;
        const char* move;
        int         threshold;
        bool        expected;
    };
    const SeeCase cases[] = {
        // Pawn takes undefended knight: wins a knight.
        {"4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", "e4d5", 320, true},
        // Queen takes pawn defended by pawn: loses queen for pawn.
        {"4k3/8/2p5/3p4/8/8/3Q4/4K3 w - - 0 1", "d2d5", 0, false},
        // Rook takes rook defended by rook: equal trade.
        {"3rk3/8/8/8/8/8/8/3RK3 w - - 0 1", "d1d8", 0, true},
        // Knight takes pawn defended by bishop: loses.
        {"4k3/8/5b2/4p3/8/3N4/8/4K3 w - - 0 1", "d3e5", 0, false},
        // X-ray: rook takes defended rook with a second rook behind.
        {"3rk3/3r4/8/8/8/8/3R4/3RK3 w - - 0 1", "d2d7", 0, true},
    };
    for (const auto& c : cases) {
        Position pos;
        pos.set(c.fen);
        Move m = find_move(pos, c.move);
        CHECK(m != Move::none(), c.fen);
        if (m != Move::none()) CHECK(pos.see_ge(m, c.threshold) == c.expected, (std::string(c.fen) + " " + c.move).c_str());
    }
}

void test_draws() {
    // Threefold repetition through knight shuffles.
    Position pos;
    const char* seq[] = {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"};
    int         n     = 0;
    for (const char* uci : seq) {
        Move m = find_move(pos, uci);
        CHECK(m != Move::none(), uci);
        pos.do_move(m);
        ++n;
        if (n == 4) CHECK(pos.st().repetition == 4, "second occurrence");
    }
    CHECK(pos.st().repetition < 0, "third occurrence is flagged");
    CHECK(pos.is_draw(0), "threefold is a draw at the root");

    pos.set("8/8/8/8/8/2k5/8/2K2B2 w - - 0 1");
    CHECK(pos.is_insufficient_material(), "KB v K");
    pos.set("8/8/8/8/8/2k5/8/2K1R3 w - - 0 1");
    CHECK(!pos.is_insufficient_material(), "KR v K");
    pos.set("8/8/8/8/8/2k5/2P5/2K5 w - - 0 1");
    CHECK(!pos.is_insufficient_material(), "KP v K");
    pos.set("4k3/8/8/8/8/8/8/R3K3 w - - 100 80");
    CHECK(pos.is_draw(1), "fifty move rule");
}

void test_bad_fens() {
    const char* bad[] = {
        "",
        "garbage",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",
        "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQ1BNR w KQkq - 0 1",
        "4k3/8/8/8/8/8/8/4K2r w - - 0 1x",
        "4k3/8/8/8/8/8/8/R3K3 b - - 0 1 extra",
        "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",
        "4k3/4Q3/8/8/8/8/8/4K3 w - - 0 1",  // side not to move in check
    };
    for (const char* fen : bad) {
        Position    pos;
        std::string error;
        bool        ok = pos.set(fen, &error);
        // Every case except the two with trailing junk must be rejected...
        std::string f = fen;
        if (f.find(" extra") != std::string::npos || f.find("1x") != std::string::npos) continue;
        CHECK(!ok, fen);
        // ...and the position must remain usable.
        CHECK(MoveList<LEGAL>(pos).size() == 20, "falls back to the start position");
    }
    // Castling rights that contradict the board are dropped, not trusted.
    Position pos;
    CHECK(pos.set("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1"), "accepted");
    CHECK(pos.castling_rights() == 0, "rights dropped");
    // Impossible en passant squares are ignored.
    CHECK(pos.set("4k3/8/8/8/8/8/8/4K3 w - e6 0 1"), "accepted");
    CHECK(pos.ep_square() == SQ_NONE, "ep dropped");
}

}  // namespace

int main() {
    test_move_tree();
    test_see();
    test_draws();
    test_bad_fens();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all unit tests passed\n");
    return EXIT_SUCCESS;
}
