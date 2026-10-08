// uci.h: Universal Chess Interface protocol handling.
#pragma once

#include <string>

#include "position.h"
#include "search.h"

namespace bastion::UCI {

constexpr const char* EngineName    = "Bastion";
constexpr const char* EngineVersion = "1.0";
constexpr const char* EngineAuthor  = "Husnain Bhatti";

// Process one line of input. Returns false when the engine should quit.
bool        handle_command(const std::string& line);
void        loop(int argc, char** argv);
std::string format_score(Value v);
std::string info_line(const Search::Info& info);
Move        parse_move(const Position& pos, const std::string& uci);
void        bench(int depth);

}  // namespace bastion::UCI
