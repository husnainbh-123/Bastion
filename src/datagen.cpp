// datagen.cpp: self-play data generation for evaluation tuning.
//
//   datagen <games> <nodes per move> <output file> [seed]
//
// Plays games against itself from randomised openings and writes quiet
// positions as "FEN | score | result" lines, where score is the search score
// and result the final game result, both from White's point of view. The
// tuner (tools/tuner) learns evaluation weights that predict those results.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "movegen.h"
#include "position.h"
#include "search.h"
#include "tt.h"

namespace bastion {

namespace {

// Search the position with the given limits and return (best move, score for the side to move).
std::pair<Move, Value> think(const Position& pos, const Search::Limits& limits) {
    Threads.start(pos, limits);
    Threads.wait();
    return {Threads.lastBestMove, Threads.main().bestValue};
}

}  // namespace

void datagen(int games, std::uint64_t nodes, const std::string& file, std::uint64_t seed) {
    std::ofstream out(file, std::ios::app);
    if (!out) return;

    std::mt19937_64 rng(seed);
    const bool      quietBefore = Threads.options.quiet;
    Threads.options.quiet       = true;
    int  written = 0, played = 0;

    for (int g = 0; g < games; ++g) {
        TT.clear();
        Threads.clear();

        // Random opening: 8 or 9 random legal plies, rejected if clearly unbalanced.
        Position pos;
        bool     usable = true;
        int      randomPlies = 8 + int(rng() % 2);
        for (int i = 0; i < randomPlies && usable; ++i) {
            MoveList<LEGAL> moves(pos);
            if (!moves.size()) usable = false;
            else pos.do_move(moves.begin()[rng() % moves.size()]);
        }
        if (!usable || !MoveList<LEGAL>(pos).size()) continue;

        Search::Limits probe;
        probe.depth = 8;
        if (std::abs(think(pos, probe).second) > 300) continue;

        std::vector<std::string> fens;
        double                   result = -1;
        int                      whiteWins = 0, blackWins = 0, drawish = 0;

        while (result < 0) {
            MoveList<LEGAL> moves(pos);
            if (!moves.size()) {
                result = !pos.in_check() ? 0.5 : pos.side_to_move() == WHITE ? 0.0 : 1.0;
                break;
            }
            if (pos.is_draw(0) || pos.game_ply() > 400) {
                result = 0.5;
                break;
            }

            Search::Limits limits;
            limits.nodes        = nodes;
            auto [best, score]  = think(pos, limits);
            const Value whiteScore = pos.side_to_move() == WHITE ? score : -score;

            // Keep quiet positions only: not in check and the best move is not a capture or promotion.
            if (!pos.in_check() && !pos.is_tactical(best) && std::abs(score) < 2000)
                fens.push_back(pos.fen() + " | " + std::to_string(whiteScore));

            // Adjudicate decided and dead-drawn games to save time.
            whiteWins = whiteScore >= 1000 ? whiteWins + 1 : 0;
            blackWins = whiteScore <= -1000 ? blackWins + 1 : 0;
            drawish   = pos.game_ply() >= 80 && std::abs(whiteScore) <= 10 ? drawish + 1 : 0;
            if (whiteWins >= 4) result = 1.0;
            else if (blackWins >= 4) result = 0.0;
            else if (drawish >= 10) result = 0.5;
            else pos.do_move(best);
        }

        const char* r = result == 1.0 ? "1.0" : result == 0.0 ? "0.0" : "0.5";
        for (const std::string& f : fens) out << f << " | " << r << '\n';
        written += int(fens.size());
        ++played;
        if (played % 100 == 0) out.flush();
    }
    Threads.options.quiet = quietBefore;
    std::printf("datagen: %d games, %d positions written to %s\n", played, written, file.c_str());
}

}  // namespace bastion
