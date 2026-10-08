// fuzz_uci.cpp: libFuzzer harness for UCI command parsing.
//
// Feeds arbitrary "position" and "setoption" commands to the protocol handler,
// which is where a GUI or the Lichess bridge passes untrusted text into the
// engine. Commands that start searches or allocate large hash tables are
// skipped to keep each run fast and bounded.
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

using namespace bastion;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    static const bool initialised = [] {
        Eval::init();
        Search::init();
        TT.resize(1);
        return true;
    }();
    (void)initialised;
    if (size > 4096) return 0;

    std::istringstream input(std::string(reinterpret_cast<const char*>(data), size));
    std::string        line;
    while (std::getline(input, line)) {
        const bool position  = line.rfind("position", 0) == 0;
        const bool safeOption = line.rfind("setoption name Move Overhead", 0) == 0 ||
                                line.rfind("setoption name Skill Level", 0) == 0;
        if (position || safeOption) UCI::handle_command(line);
    }
    return 0;
}
