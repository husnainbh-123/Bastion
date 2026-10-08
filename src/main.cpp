// main.cpp: entry point of the native engine.
#include <iostream>

#include "evaluate.h"
#include "search.h"
#include "tt.h"
#include "uci.h"

int main(int argc, char** argv) {
    using namespace bastion;
    std::ios::sync_with_stdio(false);
    Eval::init();
    Search::init();
    TT.resize(16);
    UCI::loop(argc, argv);
    return 0;
}
