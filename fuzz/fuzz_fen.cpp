// fuzz_fen.cpp: libFuzzer harness for the FEN parser.
//
// FEN strings arrive from GUIs, from Lichess and from the website, so the
// parser must reject anything malformed without crashing. Accepted positions
// must survive a round trip through fen() and every legal move must make and
// unmake cleanly. Build with -DBASTION_FUZZ=ON (clang) and run:
//   ./build/fuzz_fen fuzz/corpus/fen -max_total_time=60
#include <cstddef>
#include <cstdint>
#include <string>

#include "movegen.h"
#include "notation.h"
#include "position.h"

using namespace bastion;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 512) return 0;
    const std::string input(reinterpret_cast<const char*>(data), size);

    Position pos;
    if (!pos.set(input)) return 0;

    const std::string fen = pos.fen();
    Position          again;
    if (!again.set(fen) || again.key() != pos.key() || again.fen() != fen) __builtin_trap();

    for (const ExtMove& m : MoveList<LEGAL>(pos)) {
        move_to_san(pos, m.move);
        pos.do_move(m.move);
        pos.undo_move(m.move);
    }
    if (pos.fen() != fen) __builtin_trap();
    return 0;
}
