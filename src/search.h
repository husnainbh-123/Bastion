// search.h: iterative deepening alpha-beta search (principal variation search)
// with a transposition table, pruning, reductions and history-based move
// ordering. Multiple threads share the hash table ("Lazy SMP").
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "movegen.h"
#include "position.h"
#include "tt.h"

#ifndef BASTION_NO_THREADS
#include <thread>
#endif

namespace bastion {

namespace Search {

struct Limits {
    std::int64_t      time[2]   = {0, 0};
    std::int64_t      inc[2]    = {0, 0};
    int               movestogo = 0;
    int               depth     = 0;
    std::uint64_t     nodes     = 0;
    std::int64_t      movetime  = 0;
    bool              infinite  = false;
    std::vector<Move> searchmoves;

    bool use_time_management() const { return time[WHITE] || time[BLACK]; }
};

// Snapshot of the search, reported after each completed iteration.
struct Info {
    int               depth = 0, seldepth = 0;
    Value             score = 0;
    std::uint64_t     nodes = 0;
    std::int64_t      timeMs = 0;
    int               hashfull = 0;
    std::vector<Move> pv;
};

struct Options {
    int  threads      = 1;
    int  moveOverhead = 30;     // ms reserved for communication lag
    int  skill        = 20;     // 20 = full strength; lower levels add evaluation noise
    bool quiet        = false;  // suppress info lines (bench, data generation)
};

void init();  // precompute reduction tables

// Individual search techniques can be switched off for testing how much each
// one is worth (see tools/ablation.sh). Set BASTION_DISABLE=nmp,lmr,... in the
// environment; normal play always uses everything.
enum Feature {
    NullMove, ReverseFutility, Razoring, LateMoveReductions, LateMovePruning, Futility, SeePruning,
    HistoryPruning, SingularExtensions, IterativeReduction, AspirationWindows, Killers, HistoryOrdering,
    CheckExtensions, FeatureCount
};
extern bool Enabled[FeatureCount];
bool        disable_features(const std::string& csv);  // returns false if a name is unknown

}  // namespace Search

constexpr int HistoryMax = 16384;

using ButterflyHistory    = std::int16_t[2][64][64];  // [color][from][to]
using PieceToHistory      = std::int16_t[12][64];     // [piece][to]
using ContinuationHistory = PieceToHistory[13][64];   // [previous piece][previous to]; piece 12 = null move
using CaptureHistory      = std::int16_t[12][64][7];  // [piece][to][captured type]; 6 = none (promotion)

// Per-ply search state.
struct Stack {
    PieceToHistory* contHist;  // continuation history slice selected by the move made at this ply
    int             ply;
    Move            currentMove;
    Move            excludedMove;
    Move            killers[2];
    Value           staticEval;
    int             moveCount;
    bool            inCheck;
    bool            ttPv;
};

// One search thread: owns its history tables and a copy of the position.
class Worker {
   public:
    explicit Worker(int id) : id(id) { clear(); }
    void clear();  // forget all history (new game)
    void iterative_deepening();

    std::uint64_t node_count() const { return nodes.load(std::memory_order_relaxed); }

    int               id;
    Position          rootPos;
    std::vector<Move> rootMoves;
    int               completedDepth = 0;
    Move              bestMove       = Move::none();
    Value             bestValue      = -VALUE_INFINITE;
    int               bestSelDepth   = 0;
    std::vector<Move> bestPv;

    // Move ordering statistics, read by the MovePicker.
    ButterflyHistory    mainHistory;
    CaptureHistory      captureHistory;
    ContinuationHistory contHist;
    Move                counterMoves[12][64];

   private:
    friend class ThreadPool;

    template <bool PvNode>
    Value search(Position& pos, Stack* ss, Value alpha, Value beta, int depth, bool cutNode);
    template <bool PvNode>
    Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta);

    void  update_pv(int ply, Move m);
    void  update_quiet_stats(const Position& pos, Stack* ss, Move best, int bonus, const Move* quiets, int quietCount);
    void  update_capture_stats(const Position& pos, Move best, int bonus, const Move* captures, int captureCount);
    void  update_continuation(Stack* ss, Piece pc, Square to, int bonus);
    Value evaluate(const Position& pos) const;
    bool  should_stop();
    void  count_node() { nodes.store(nodes.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed); }

    std::atomic<std::uint64_t> nodes{0};
    int                        selDepth  = 0;
    int                        rootDepth = 0;
    int                        nmpMinPly = 0;
    Move                       iterBestMove  = Move::none();  // best root move of the running iteration
    Value                      iterBestValue = VALUE_NONE;
    Move                       pvTable[MAX_PLY + 1][MAX_PLY + 1];
    int                        pvLength[MAX_PLY + 1];
    std::uint64_t              rootMoveNodes[64][64];
};

// Owns the workers, starts/stops searches and reports results.
class ThreadPool {
   public:
    using InfoCallback     = std::function<void(const Search::Info&)>;
    using BestMoveCallback = std::function<void(Move best, Move ponder)>;

    ThreadPool();
    ~ThreadPool();

    void set_threads(int n);
    void clear();  // new game: reset histories
    void start(const Position& pos, const Search::Limits& limits);
    void stop() { stopFlag = true; }
    void wait();  // block until the current search has finished and bestmove was reported
    bool searching() const { return isSearching; }

    std::uint64_t nodes_searched() const;
    std::int64_t  elapsed() const;

    Search::Options  options;
    InfoCallback     onInfo;
    BestMoveCallback onBestMove;

    // Shared state read by the workers
    std::atomic<bool> stopFlag{false};
    Search::Limits    limits;
    std::int64_t      startTime   = 0;
    std::int64_t      optimumTime = 0, maximumTime = 0;
    std::uint64_t     noiseSeed   = 0;
    Move              lastBestMove = Move::none();  // result of the most recent search

    Worker& main() { return *workers[0]; }
    std::vector<std::unique_ptr<Worker>> workers;

   private:
    void run_main();
    void init_time(Color us);
    std::atomic<bool> isSearching{false};
#ifndef BASTION_NO_THREADS
    std::thread              mainThread;
    std::vector<std::thread> helperThreads;
#endif
};

extern ThreadPool Threads;

std::int64_t now_ms();

}  // namespace bastion
