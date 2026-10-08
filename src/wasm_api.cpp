// wasm_api.cpp: the interface the website uses when Bastion runs as
// WebAssembly. Everything the page needs - legal moves, notation, game-over
// detection and the search itself - comes from the engine, so the browser and
// the native engine share one implementation of the rules.
//
// Results are returned as JSON strings; search progress is streamed to
// JavaScript through the imported js_emit() function.
#include <cstdlib>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "movegen.h"
#include "notation.h"
#include "position.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

#define BASTION_EXPORT(name) extern "C" __attribute__((export_name(#name)))

extern "C" __attribute__((import_module("env"), import_name("js_emit"))) void js_emit(const char* data, int length);

// The WASI C++ runtime is built without exception support, but operator new
// still refers to the throwing machinery. Running out of memory is fatal anyway.
extern "C" void* __cxa_allocate_exception(std::size_t) { std::abort(); }
extern "C" void  __cxa_throw(void*, void*, void (*)(void*)) { std::abort(); }
extern "C" void  __cxa_rethrow() { std::abort(); }

using namespace bastion;

namespace {

std::string returned;      // storage for strings handed back to JavaScript
Position    searchRoot;    // root of the running search, used to write the PV in SAN

void emit(const std::string& json) { js_emit(json.data(), int(json.size())); }

bool load(Position& pos, const char* fen, const char* moves) {
    if (!pos.set(fen && *fen ? fen : StartFEN)) return false;
    std::istringstream ss(moves ? moves : "");
    std::string        token;
    while (ss >> token) {
        Move m = UCI::parse_move(pos, token);
        if (m == Move::none()) return false;
        pos.do_move(m);
    }
    return true;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + '"';
}

std::string pv_json(const std::vector<Move>& pv) {
    Position    p = searchRoot;
    std::string san = "[", uci = "[";
    for (std::size_t i = 0; i < pv.size(); ++i) {
        if (!p.pseudo_legal(pv[i]) || !p.legal(pv[i])) break;
        san += (i ? "," : "") + quoted(move_to_san(p, pv[i]));
        uci += (i ? "," : "") + quoted(move_to_uci(pv[i]));
        p.do_move(pv[i]);
    }
    return "\"pv\":" + san + "],\"pvUci\":" + uci + "]";
}

}  // namespace

BASTION_EXPORT(bastion_init) void bastion_init() {
    Eval::init();
    Search::init();
    TT.resize(32);
    Threads.onInfo = [](const Search::Info& info) {
        std::ostringstream ss;
        int                mate = 0;
        if (info.score >= VALUE_MATE_IN_MAX_PLY) mate = (VALUE_MATE - info.score + 1) / 2;
        if (info.score <= VALUE_MATED_IN_MAX_PLY) mate = -(VALUE_MATE + info.score) / 2;
        ss << "{\"type\":\"info\",\"depth\":" << info.depth << ",\"seldepth\":" << info.seldepth
           << ",\"score\":" << (mate ? 0 : info.score) << ",\"mate\":" << mate << ",\"nodes\":" << info.nodes
           << ",\"time\":" << info.timeMs << "," << pv_json(info.pv) << "}";
        emit(ss.str());
    };
    Threads.onBestMove = [](Move best, Move) {
        Position p = searchRoot;
        std::string san = best ? move_to_san(p, best) : "";
        emit("{\"type\":\"bestmove\",\"move\":" + quoted(best ? move_to_uci(best) : "") + ",\"san\":" + quoted(san) +
             ",\"nodes\":" + std::to_string(Threads.nodes_searched()) + "}");
    };
}

BASTION_EXPORT(bastion_alloc) void* bastion_alloc(int size) { return std::malloc(std::size_t(size)); }
BASTION_EXPORT(bastion_free) void bastion_free(void* p) { std::free(p); }

BASTION_EXPORT(bastion_new_game) void bastion_new_game() {
    TT.clear();
    Threads.clear();
}

// Legal moves (UCI and SAN), side to move, check and game result for a position.
BASTION_EXPORT(bastion_state) const char* bastion_state(const char* fen, const char* moves) {
    Position pos;
    if (!load(pos, fen, moves)) {
        returned = "{\"ok\":false}";
        return returned.c_str();
    }
    GameStatus  status = game_status(pos);
    std::string legal  = "[";
    bool        first  = true;
    for (const ExtMove& m : MoveList<LEGAL>(pos)) {
        legal += std::string(first ? "" : ",") + "[" + quoted(move_to_uci(m)) + "," + quoted(move_to_san(pos, m)) + "]";
        first = false;
    }
    returned = "{\"ok\":true,\"fen\":" + quoted(pos.fen()) + ",\"turn\":\"" + (pos.side_to_move() == WHITE ? "w" : "b") +
               "\",\"check\":" + (pos.in_check() ? "true" : "false") + ",\"legal\":" + legal + "],\"result\":" +
               quoted(status.result) + ",\"reason\":" + quoted(status.reason) + "}";
    return returned.c_str();
}

// Search synchronously; progress and the final move arrive through js_emit().
BASTION_EXPORT(bastion_search) int bastion_search(const char* fen, const char* moves, int movetimeMs, int depth,
                                                   int skill) {
    if (!load(searchRoot, fen, moves)) return 0;
    Search::Limits limits;
    limits.movetime       = movetimeMs > 0 ? movetimeMs : 0;
    limits.depth          = depth > 0 ? depth : 0;
    if (!limits.movetime && !limits.depth) limits.movetime = 1000;
    Threads.options.skill = skill < 0 ? 0 : skill > 20 ? 20 : skill;
    Threads.start(searchRoot, limits);
    return 1;
}

BASTION_EXPORT(bastion_eval) int bastion_eval(const char* fen, const char* moves) {
    Position pos;
    if (!load(pos, fen, moves)) return 0;
    return Eval::evaluate(pos);
}
