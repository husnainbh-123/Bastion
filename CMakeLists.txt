cmake_minimum_required(VERSION 3.16)
project(Bastion VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BASTION_NATIVE   "Optimise for the CPU of the build machine (-march=native)" OFF)
option(BASTION_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(BASTION_FUZZ     "Build libFuzzer targets (requires clang)" OFF)
set(BASTION_ARCH "" CACHE STRING "Target architecture for -march, e.g. x86-64-v2 or x86-64-v3")

set(ENGINE_SOURCES
  src/bitboard.cpp src/position.cpp src/movegen.cpp src/evaluate.cpp src/tt.cpp
  src/movepick.cpp src/search.cpp src/uci.cpp src/bench.cpp src/datagen.cpp src/notation.cpp)

add_library(bastion_core STATIC ${ENGINE_SOURCES})
target_include_directories(bastion_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(bastion_core PUBLIC Threads::Threads)

if(MSVC)
  target_compile_options(bastion_core PUBLIC /W4 /permissive- /EHsc)
  if(BASTION_ARCH STREQUAL "x86-64-v3")
    target_compile_options(bastion_core PUBLIC /arch:AVX2)
  endif()
else()
  target_compile_options(bastion_core PUBLIC -Wall -Wextra -Wpedantic)
  if(BASTION_NATIVE)
    target_compile_options(bastion_core PUBLIC -march=native)
  elseif(BASTION_ARCH)
    target_compile_options(bastion_core PUBLIC -march=${BASTION_ARCH})
  endif()
endif()

if(BASTION_SANITIZE)
  target_compile_options(bastion_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(bastion_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(bastion src/main.cpp)
target_link_libraries(bastion PRIVATE bastion_core)

# ---- Tests ----
enable_testing()
add_executable(perft_tests tests/perft_tests.cpp)
target_link_libraries(perft_tests PRIVATE bastion_core)
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE bastion_core)
add_test(NAME perft COMMAND perft_tests)
add_test(NAME unit  COMMAND unit_tests)
add_test(NAME bench COMMAND bastion bench 8)

# ---- Fuzzing (clang -fsanitize=fuzzer) ----
if(BASTION_FUZZ)
  foreach(target fuzz_fen fuzz_uci)
    add_executable(${target} fuzz/${target}.cpp)
    target_link_libraries(${target} PRIVATE bastion_core)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(${target} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()

# ---- Tools ----
add_executable(tuner tools/tuner/tuner.cpp)
target_link_libraries(tuner PRIVATE bastion_core)
