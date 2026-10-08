// uci.cpp: the UCI command loop, plus a few debugging commands
// ("d", "eval", "perft", "bench").
#include "uci.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>

#include "evaluate.h"
#include "movegen.h"
#include "tt.h"

namespace bastion {

extern const std::vector<std::string> BenchPositions;
#ifndef BASTION_WEB
void datagen(int games, std::uint64_t nodes, const std::string& file, std::uint64_t seed);
#endif

namespace UCI {

namespace {

Position pos;
#ifndef BASTION_NO_THREADS
std::mutex outputMutex;
#endif

void say(const std::string& s) {
#ifndef BASTION_NO_THREADS
    std::lock_guard<std::mutex> lock(outputMutex);
#endif
    std::cout << s << std::endl;
}

std::string lowercase(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// Parse an integer option value safely (no exceptions on bad input).
bool to_int(const std::string& s, long long& out) {
    if (s.empty() || s.size() > 18) return false;
    std::size_t i   = s[0] == '-' ? 1 : 0;
    long long   val = 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        val = val * 10 + (s[i] - '0');
    }
    out = s[0] == '-' ? -val : val;
    return true;
}

void print_options() {
    say("option name Hash type spin default 16 min 1 max 65536");
    say("option name Threads type spin default 1 min 1 max 256");
    say("option name Move Overhead type spin default 30 min 0 max 5000");
    say("option name Skill Level type spin default 20 min 0 max 20");
    say("option name Clear Hash type button");
}

void set_option(std::istringstream& is) {
    std::string token, name, value;
    is >> token;  // "name"
    while (is >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    while (is >> token) value += (value.empty() ? "" : " ") + token;
    name = lowercase(name);

    long long n = 0;
    bool      numeric = to_int(value, n);
    if (name == "clear hash") {
        TT.clear();
        return;
    }
    if (!numeric) {
        say("info string invalid value for option " + name);
        return;
    }
    Threads.wait();
    if (name == "hash") TT.resize(std::size_t(std::clamp<long long>(n, 1, 65536)));
    else if (name == "threads") Threads.set_threads(int(std::clamp<long long>(n, 1, 256)));
    else if (name == "move overhead" || name == "moveoverhead")
        Threads.options.moveOverhead = int(std::clamp<long long>(n, 0, 5000));
    else if (name == "skill level") Threads.options.skill = int(std::clamp<long long>(n, 0, 20));
    else say("info string unknown option " + name);
}

void set_position(std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = StartFEN;
        is >> token;  // "moves" (if present)
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
    } else
        return;

    Position    next;
    std::string error;
    if (!next.set(fen, &error)) {
        say("info string invalid fen: " + error);
        return;
    }
    while (is >> token) {
        Move m = parse_move(next, token);
        if (m == Move::none()) {
            say("info string illegal move " + token);
            break;
        }
        next.do_move(m);
    }
    pos = next;
}

void go(std::istringstream& is) {
    Search::Limits limits;
    std::string    token;
    long long      n;
    auto           next_int = [&](long long& out) {
        std::string v;
        return (is >> v) && to_int(v, out);
    };
    while (is >> token) {
        if (token == "wtime" && next_int(n)) limits.time[WHITE] = std::max(0LL, n);
        else if (token == "btime" && next_int(n)) limits.time[BLACK] = std::max(0LL, n);
        else if (token == "winc" && next_int(n)) limits.inc[WHITE] = std::max(0LL, n);
        else if (token == "binc" && next_int(n)) limits.inc[BLACK] = std::max(0LL, n);
        else if (token == "movestogo" && next_int(n)) limits.movestogo = int(std::clamp(n, 0LL, 1000LL));
        else if (token == "depth" && next_int(n)) limits.depth = int(std::clamp(n, 1LL, (long long)MAX_PLY - 10));
        else if (token == "nodes" && next_int(n)) limits.nodes = std::uint64_t(std::max(1LL, n));
        else if (token == "movetime" && next_int(n)) limits.movetime = std::max(1LL, n);
        else if (token == "infinite") limits.infinite = true;
        else if (token == "searchmoves")
            while (is >> token) {
                Move m = parse_move(pos, token);
                if (m != Move::none()) limits.searchmoves.push_back(m);
            }
    }
#ifdef BASTION_NO_THREADS
    // A single-threaded build cannot receive "stop" while searching.
    if (limits.infinite && !limits.depth && !limits.nodes && !limits.movetime) limits.depth = 64;
    limits.infinite = false;
#endif
    Threads.start(pos, limits);
}

std::uint64_t perft(Position& p, int depth) {
    MoveList<LEGAL> moves(p);
    if (depth <= 1) return depth == 1 ? moves.size() : 1;
    std::uint64_t total = 0;
    for (const ExtMove& m : moves) {
        p.do_move(m);
        total += perft(p, depth - 1);
        p.undo_move(m);
    }
    return total;
}

void perft_divide(int depth) {
    std::int64_t  start = now_ms();
    std::uint64_t total = 0;
    Position      p     = pos;
    for (const ExtMove& m : MoveList<LEGAL>(p)) {
        p.do_move(m);
        std::uint64_t n = perft(p, depth - 1);
        p.undo_move(m);
        total += n;
        say(move_to_uci(m) + ": " + std::to_string(n));
    }
    std::int64_t ms = std::max<std::int64_t>(1, now_ms() - start);
    say("\nNodes searched: " + std::to_string(total) + " (" + std::to_string(total * 1000 / ms) + " nps)");
}

}  // namespace

Move parse_move(const Position& p, const std::string& str) {
    std::string s = lowercase(str);
    for (const ExtMove& m : MoveList<LEGAL>(p))
        if (move_to_uci(m) == s) return m;
    return Move::none();
}

std::string format_score(Value v) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return "mate " + std::to_string((VALUE_MATE - v + 1) / 2);
    if (v <= VALUE_MATED_IN_MAX_PLY) return "mate " + std::to_string(-(VALUE_MATE + v) / 2);
    return "cp " + std::to_string(v);
}

std::string info_line(const Search::Info& info) {
    std::ostringstream ss;
    std::int64_t       ms = std::max<std::int64_t>(1, info.timeMs);
    ss << "info depth " << info.depth << " seldepth " << info.seldepth << " multipv 1 score " << format_score(info.score)
       << " nodes " << info.nodes << " nps " << info.nodes * 1000 / std::uint64_t(ms) << " hashfull " << info.hashfull
       << " time " << info.timeMs << " pv";
    for (Move m : info.pv) ss << ' ' << move_to_uci(m);
    return ss.str();
}

void bench(int depth) {
    // Fixed-depth searches over a set of positions. The total node count acts as a
    // fingerprint of the search: any functional change alters it.
    const bool    quietBefore = Threads.options.quiet;
    std::uint64_t nodes       = 0;
    std::int64_t  elapsed     = 0;
    Threads.options.quiet     = true;
    for (const std::string& fen : BenchPositions) {
        Position p;
        p.set(fen);
        TT.clear();
        Threads.clear();
        Search::Limits limits;
        limits.depth = depth;
        Threads.start(p, limits);
        Threads.wait();
        nodes += Threads.nodes_searched();
        elapsed += Threads.elapsed();
    }
    Threads.options.quiet = quietBefore;
    TT.clear();
    Threads.clear();
    elapsed = std::max<std::int64_t>(1, elapsed);
    say(std::to_string(nodes) + " nodes " + std::to_string(nodes * 1000 / std::uint64_t(elapsed)) + " nps");
}

bool handle_command(const std::string& line) {
    std::istringstream is(line);
    std::string        token;
    if (!(is >> token)) return true;

    if (token == "uci") {
        say(std::string("id name ") + EngineName + " " + EngineVersion);
        say(std::string("id author ") + EngineAuthor);
        print_options();
        say("uciok");
    } else if (token == "isready")
        say("readyok");
    else if (token == "ucinewgame") {
        Threads.wait();
        TT.clear();
        Threads.clear();
    } else if (token == "setoption")
        set_option(is);
    else if (token == "position") {
        Threads.wait();
        set_position(is);
    } else if (token == "go")
        go(is);
    else if (token == "stop") {
        Threads.stop();
        Threads.wait();
    } else if (token == "ponderhit") {
        // Pondering is not supported; nothing to do.
    } else if (token == "quit") {
        Threads.stop();
        Threads.wait();
        return false;
    } else if (token == "d")
        say(pos.pretty());
    else if (token == "eval")
        say(Eval::describe(pos));
    else if (token == "perft") {
        long long d = 0;
        std::string v;
        if ((is >> v) && to_int(v, d) && d >= 1 && d <= 10) perft_divide(int(d));
        else say("info string usage: perft <depth 1-10>");
    } else if (token == "bench") {
        long long d = 13;
        std::string v;
        if (is >> v) to_int(v, d);
        bench(int(std::clamp(d, 1LL, 30LL)));
    }
#ifndef BASTION_WEB
    else if (token == "datagen") {
        long long games = 0, nodes = 0, seed = 1;
        std::string g, n, file, sd;
        if ((is >> g >> n >> file) && to_int(g, games) && to_int(n, nodes) && games > 0 && nodes > 0) {
            if (is >> sd) to_int(sd, seed);
            datagen(int(games), std::uint64_t(nodes), file, std::uint64_t(seed));
        } else
            say("info string usage: datagen <games> <nodes> <file> [seed]");
    }
#endif
    else
        say("info string unknown command: " + token);
    return true;
}

void loop(int argc, char** argv) {
    Threads.onInfo     = [](const Search::Info& info) { say(info_line(info)); };
    Threads.onBestMove = [](Move best, Move ponder) {
        std::string s = "bestmove " + move_to_uci(best);
        if (ponder) s += " ponder " + move_to_uci(ponder);
        say(s);
    };

    // Command line arguments are executed as commands, e.g. "bastion bench 12".
    if (argc > 1) {
        std::string cmd;
        for (int i = 1; i < argc; ++i) cmd += std::string(i > 1 ? " " : "") + argv[i];
        handle_command(cmd);
        Threads.wait();
        return;
    }

    std::string line;
    while (std::getline(std::cin, line))
        if (!handle_command(line)) break;
    Threads.stop();
    Threads.wait();
}

}  // namespace UCI
}  // namespace bastion
