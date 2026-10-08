# Simple build without CMake. Usage:
#   make                 # optimised for this machine
#   make EXE=bastion-dev # custom output name (used by testing tools such as OpenBench)
#   make wasm            # WebAssembly build for the website (needs clang + wasi-libc)
CXX      ?= g++
EXE      ?= bastion
ARCH     ?= native
SRC      := $(filter-out src/wasm_api.cpp,$(wildcard src/*.cpp))
CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -flto -march=$(ARCH) -pthread -Wall -Wextra
LDFLAGS  ?= -pthread -flto

ifeq ($(OS),Windows_NT)
  LDFLAGS += -static
  EXE := $(EXE).exe
endif

$(EXE): $(SRC) $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(EXE) $(LDFLAGS)

WASM_CXX   ?= clang++
WASM_SYSROOT ?= /usr
WASM_SRC   := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
wasm: web/engine/bastion.wasm
web/engine/bastion.wasm: $(WASM_SRC) $(wildcard src/*.h)
	mkdir -p web/engine
	$(WASM_CXX) --target=wasm32-wasi --sysroot=$(WASM_SYSROOT) -std=c++20 -O3 -DNDEBUG -DBASTION_NO_THREADS \
	  -fno-exceptions -mexec-model=reactor -Wl,--export-dynamic -Wl,--strip-all \
	  $(WASM_SRC) -o $@ -lc++abi

clean:
	rm -f $(EXE) $(EXE).exe web/engine/bastion.wasm

.PHONY: clean wasm
