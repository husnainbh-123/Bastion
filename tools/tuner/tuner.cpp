// tuner.cpp: Texel-style evaluation tuner.
//
//   tuner <data file> [max positions] [epochs] [threads] > src/eval_params.h
//
// The data file holds "FEN | score | result" lines written by the engine's
// "datagen" command. For every position we record which evaluation terms are
// present (the trace), so the static evaluation becomes a linear function of
// the weights. We then minimise the mean squared error between
//     sigmoid(K * eval)   and   the game result (1, 0.5 or 0)
// with full-batch gradient descent (Adam). K is fitted first so that the
// evaluation scale stays in centipawns.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/evaluate.h"
#include "../../src/position.h"

using namespace bastion;

namespace {

struct Entry {
    std::uint32_t start;   // first feature in the shared arrays
    std::uint16_t count;   // number of features
    std::uint8_t  phase;   // 0..24
    std::uint8_t  scale;   // 0..128
    float         extraEg; // fixed (untuned) endgame term
    float         result;  // 1, 0.5, 0 from white's point of view
};

std::vector<Entry>         entries;
std::vector<std::uint16_t> featIndex;
std::vector<std::int8_t>   featCoeff;

constexpr int N = Eval::NumParams;
double        mg[N], eg[N];

double sigmoid(double k, double e) { return 1.0 / (1.0 + std::pow(10.0, -k * e / 400.0)); }

double evaluate_entry(const Entry& en) {
    double m = 0, e = en.extraEg;
    for (std::uint32_t i = en.start; i < en.start + en.count; ++i) {
        m += featCoeff[i] * mg[featIndex[i]];
        e += featCoeff[i] * eg[featIndex[i]];
    }
    return (m * en.phase + e * en.scale / 128.0 * (24 - en.phase)) / 24.0;
}

double total_error(double k, int threads) {
    std::vector<double>      partial(threads, 0.0);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            double sum = 0;
            for (std::size_t i = t; i < entries.size(); i += threads) {
                double d = entries[i].result - sigmoid(k, evaluate_entry(entries[i]));
                sum += d * d;
            }
            partial[t] = sum;
        });
    for (auto& th : pool) th.join();
    double sum = 0;
    for (double p : partial) sum += p;
    return sum / double(entries.size());
}

double fit_k(int threads) {
    double lo = 0.2, hi = 3.0;
    for (int it = 0; it < 40; ++it) {  // golden-section search
        double a = hi - (hi - lo) / 1.618, b = lo + (hi - lo) / 1.618;
        if (total_error(a, threads) < total_error(b, threads)) hi = b;
        else lo = a;
    }
    return (lo + hi) / 2;
}

void gradient(double k, int threads, std::vector<double>& gmg, std::vector<double>& geg) {
    std::vector<std::vector<double>> pm(threads, std::vector<double>(N)), pe(threads, std::vector<double>(N));
    std::vector<std::thread>         pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([&, t] {
            auto& lm = pm[t];
            auto& le = pe[t];
            for (std::size_t i = t; i < entries.size(); i += threads) {
                const Entry& en = entries[i];
                double       s  = sigmoid(k, evaluate_entry(en));
                double       d  = (en.result - s) * s * (1 - s);  // common factor
                double       wm = d * en.phase / 24.0;
                double       we = d * en.scale / 128.0 * (24 - en.phase) / 24.0;
                for (std::uint32_t j = en.start; j < en.start + en.count; ++j) {
                    lm[featIndex[j]] += wm * featCoeff[j];
                    le[featIndex[j]] += we * featCoeff[j];
                }
            }
        });
    for (auto& th : pool) th.join();
    const double scale = -2.0 * std::log(10.0) * k / 400.0 / double(entries.size());
    for (int i = 0; i < N; ++i) {
        gmg[i] = geg[i] = 0;
        for (int t = 0; t < threads; ++t) {
            gmg[i] += pm[t][i];
            geg[i] += pe[t][i];
        }
        gmg[i] *= scale;
        geg[i] *= scale;
    }
}

// ---- Output ----------------------------------------------------------------
struct Field {
    const char* name;  // member name in Eval::Params
    int         offset;
    int         count;
    bool        board;  // printed as an 8x8 board (visual layout, rank 8 first)
};

#define FIELD(member, isBoard)                                                               \
    Field{#member, int(reinterpret_cast<const Score*>(&ref.member) - reinterpret_cast<const Score*>(&ref)), \
          int(sizeof(ref.member) / sizeof(Score)), isBoard}

std::string S(int i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "S(%4d,%4d)", int(std::lround(mg[i])), int(std::lround(eg[i])));
    return buf;
}

void print_params(double k, double before, double after, std::size_t positions) {
    static Eval::Params ref;
    const Field fields[] = {
        FIELD(pieceValue, false), FIELD(psqt, true), FIELD(knightMobility, false), FIELD(bishopMobility, false),
        FIELD(rookMobility, false), FIELD(queenMobility, false), FIELD(bishopPair, false),
        FIELD(bishopPawnsSameColor, false), FIELD(knightOutpost, false), FIELD(bishopOutpost, false),
        FIELD(rookOpenFile, false), FIELD(rookSemiOpenFile, false), FIELD(rookOnSeventh, false),
        FIELD(doubledPawn, false), FIELD(isolatedPawn, false), FIELD(backwardPawn, false),
        FIELD(connectedPawn, false), FIELD(passedPawn, false), FIELD(passedFreePath, false),
        FIELD(passedOwnKingDistance, false), FIELD(passedEnemyKingDistance, false), FIELD(kingAttackers, false),
        FIELD(kingAttackWeight, false), FIELD(safeCheck, false), FIELD(kingShelter, false), FIELD(kingStorm, false),
        FIELD(kingOpenFile, false), FIELD(threatByPawn, false), FIELD(threatByMinor, false),
        FIELD(threatByRook, false), FIELD(threatByKing, false), FIELD(hangingPiece, false), FIELD(tempo, false),
    };
    int covered = 0;
    for (const Field& f : fields) covered += f.count;
    if (covered != N) {
        std::fprintf(stderr, "error: field table covers %d of %d parameters\n", covered, N);
        std::exit(1);
    }

    std::printf("// eval_params.h: evaluation weights.\n//\n");
    std::printf("// GENERATED FILE - written by tools/tuner from %zu self-play positions.\n", positions);
    std::printf("// Mean squared error %.6f -> %.6f (K = %.4f). Tables are laid out\n", before, after, k);
    std::printf("// visually: the first row is rank 8, a8..h8.\n");
    std::printf("#pragma once\n\n#include \"evaluate.h\"\n\nnamespace bastion::Eval {\n\n");
    std::printf("inline void set_default_params(Params& p) {\n");
    std::printf("    Score* base = reinterpret_cast<Score*>(&p);\n    (void)base;\n");
    for (const Field& f : fields) {
        if (f.board) {
            // psqt: 6 boards, stored a1-first in memory but printed rank 8 first
            std::printf("    static const Score %s[%d] = {\n", f.name, f.count);
            for (int b = 0; b < f.count / 64; ++b) {
                for (int r = 7; r >= 0; --r) {
                    std::printf("        ");
                    for (int file = 0; file < 8; ++file) std::printf("%s, ", S(f.offset + b * 64 + r * 8 + file).c_str());
                    std::printf("\n");
                }
                if (b + 1 < f.count / 64) std::printf("\n");
            }
            std::printf("    };\n");
            std::printf("    for (int b = 0; b < %d; ++b)\n        for (int s = 0; s < 64; ++s) p.%s[b][s] = %s[b * 64 + (s ^ 56)];\n",
                        f.count / 64, f.name, f.name);
        } else if (f.count == 1) {
            std::printf("    p.%s = %s;\n", f.name, S(f.offset).c_str());
        } else {
            std::printf("    static const Score %s[%d] = {", f.name, f.count);
            for (int i = 0; i < f.count; ++i) std::printf("%s%s", i % 8 == 0 ? "\n        " : "", (S(f.offset + i) + ", ").c_str());
            std::printf("\n    };\n    for (int i = 0; i < %d; ++i) p.%s[i] = %s[i];\n", f.count, f.name, f.name);
        }
    }
    std::printf("}\n\n}  // namespace bastion::Eval\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: tuner <data file> [max positions] [epochs] [threads]\n");
        return 1;
    }
    const std::size_t maxPositions = argc > 2 ? std::stoull(argv[2]) : 50'000'000;
    const int         epochs       = argc > 3 ? std::stoi(argv[3]) : 2000;
    const int         threads      = argc > 4 ? std::stoi(argv[4]) : 2;

    Eval::init();
    const Score* base = reinterpret_cast<const Score*>(&Eval::P);
    for (int i = 0; i < N; ++i) mg[i] = mg_value(base[i]), eg[i] = eg_value(base[i]);

    // ---- Load data and build traces ----
    std::ifstream in(argv[1]);
    std::string   line;
    Position      pos;
    Eval::Trace   trace;
    std::size_t   lines = 0, mismatches = 0;
    while (std::getline(in, line) && entries.size() < maxPositions) {
        ++lines;
        std::size_t a = line.find('|'), b = line.rfind('|');
        if (a == std::string::npos || a == b) continue;
        std::string fen    = line.substr(0, a);
        double      result = std::stod(line.substr(b + 1));
        if (!pos.set(fen)) continue;

        Value white = Eval::evaluate_trace(pos, trace);
        Entry en;
        en.start   = std::uint32_t(featIndex.size());
        en.phase   = std::uint8_t(trace.phase);
        en.scale   = std::uint8_t(trace.scale);
        en.extraEg = float(eg_value(trace.extra));
        en.result  = float(result);
        for (int i = 0; i < N; ++i) {
            int c = trace.coeff[i][WHITE] - trace.coeff[i][BLACK];
            if (c) {
                featIndex.push_back(std::uint16_t(i));
                featCoeff.push_back(std::int8_t(std::clamp(c, -127, 127)));
            }
        }
        en.count = std::uint16_t(featIndex.size() - en.start);
        entries.push_back(en);
        // The linear model must reproduce the real evaluation (up to rounding).
        if (std::abs(evaluate_entry(en) - white) > 2) ++mismatches;
        if (entries.size() % 500000 == 0) std::fprintf(stderr, "loaded %zu positions\n", entries.size());
    }
    std::fprintf(stderr, "%zu positions (%zu lines), %.1f features each, %zu model mismatches\n", entries.size(),
                 lines, double(featIndex.size()) / std::max<std::size_t>(1, entries.size()), mismatches);
    if (entries.empty()) return 1;

    const double k      = fit_k(threads);
    const double before = total_error(k, threads);
    std::fprintf(stderr, "K = %.4f, initial error %.6f\n", k, before);

    // ---- Adam ----
    std::vector<double> gmg(N), geg(N), m1(2 * N, 0), m2(2 * N, 0);
    const double        beta1 = 0.9, beta2 = 0.999, eps = 1e-8;
    double              lr    = 1.0;
    for (int epoch = 1; epoch <= epochs; ++epoch) {
        gradient(k, threads, gmg, geg);
        for (int i = 0; i < 2 * N; ++i) {
            double  g = i < N ? gmg[i] : geg[i - N];
            double& p = i < N ? mg[i] : eg[i - N];
            m1[i]     = beta1 * m1[i] + (1 - beta1) * g;
            m2[i]     = beta2 * m2[i] + (1 - beta2) * g * g;
            double mh = m1[i] / (1 - std::pow(beta1, epoch));
            double vh = m2[i] / (1 - std::pow(beta2, epoch));
            p -= lr * mh / (std::sqrt(vh) + eps);
        }
        if (epoch % 250 == 0) lr *= 0.75;
        if (epoch % 50 == 0 || epoch == epochs)
            std::fprintf(stderr, "epoch %5d  error %.6f\n", epoch, total_error(k, threads));
    }
    const double after = total_error(k, threads);
    std::fprintf(stderr, "final error %.6f (was %.6f)\n", after, before);
    print_params(k, before, after, entries.size());
    return 0;
}
