cmake_minimum_required(VERSION 3.20)
project(TradingSystem)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Sanitizer Build Option ─────────────────────────────────────────────────────
# Usage:  cmake -DSANITIZE=asan  -S . -B build-asan
#         cmake -DSANITIZE=tsan  -S . -B build-tsan
#         cmake -DSANITIZE=ubsan -S . -B build-ubsan
#
# ASan  (AddressSanitizer)  — detects heap/stack overflows, use-after-free
# TSan  (ThreadSanitizer)   — detects data races between threads
# UBSan (UBSanitizer)       — catches undefined behaviour (signed overflow, bad cast, etc.)
#
# Run any of these before touching multi-threaded or pointer-heavy code.
set(SANITIZE "" CACHE STRING "Sanitizer to enable: asan | tsan | ubsan | none")
if(SANITIZE STREQUAL "asan")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizer: AddressSanitizer + UBSanitizer enabled")
elseif(SANITIZE STREQUAL "tsan")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "Sanitizer: ThreadSanitizer enabled")
elseif(SANITIZE STREQUAL "ubsan")
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
    message(STATUS "Sanitizer: UBSanitizer enabled")
endif()

# ── Global include for header-only utilities (SpscQueue, ObjectPool, cpu_affinity) ──
include_directories(${CMAKE_SOURCE_DIR}/libs/utils/include)

# ── Boost ──────────────────────────────────────────────────────────────────────
# Boost.Lockfree: wait-free SPSC and lock-free MPMC queues (header-only)
# Boost.Beast:    WebSocket server for the live metrics dashboard (header-only)
# Boost.Asio:     async I/O layer used by Beast (header-only)
# Boost.System:   error_code / system_error (compiled, required by Beast)
#
# macOS:  brew install boost
# Linux:  sudo apt-get install libboost-dev libboost-system-dev
# Since Boost 1.75, Boost.System is header-only — no compiled library.
# Beast and Asio are also header-only. We only need the header package.
find_package(Boost 1.80 REQUIRED COMPONENTS headers
             HINTS /opt/homebrew /usr/local)
message(STATUS "Boost ${Boost_VERSION} — ${Boost_INCLUDE_DIRS}")
include_directories(${Boost_INCLUDE_DIRS})

# ── LTO (Link-Time Optimisation) ──────────────────────────────────────────────
# Merges all translation units at link time → enables cross-module inlining.
# Example: acceptNewOrder() can be inlined into the FIX reader callback,
# eliminating the function-call boundary entirely on the hot path.
# Only active in Release because LTO adds significant compile + link time.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_OK OUTPUT LTO_ERR)
    if(LTO_OK)
        message(STATUS "LTO enabled")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO unavailable: ${LTO_ERR}")
    endif()
endif()

# ── PGO (Profile-Guided Optimisation) ─────────────────────────────────────────
# Two-pass compilation. Pass 1 instruments the binary to record branch
# frequencies and call counts at runtime. Pass 2 uses that data to reshape
# the binary: inline hot call sites, reorder branches, cluster hot code.
#
# Step 1 — collect:
#   cmake -DPGO_GENERATE=ON -DCMAKE_BUILD_TYPE=Release -S . -B build-pgo
#   cmake --build build-pgo
#   ./build-pgo/src/trading-system           # run with production-like data
#   llvm-profdata merge -output=default.profdata *.profraw   # Clang/macOS only
#
# Step 2 — apply:
#   cmake -DPGO_USE=ON -DCMAKE_BUILD_TYPE=Release -S . -B build-release
#   cmake --build build-release
option(PGO_GENERATE "Instrument binary for PGO profile collection" OFF)
option(PGO_USE      "Apply collected PGO profile"                  OFF)
set(PGO_PROFILE_DIR "${CMAKE_SOURCE_DIR}/build-pgo"
    CACHE PATH "Directory containing default.profdata (Clang) or *.gcda (GCC)")

if(PGO_GENERATE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fprofile-instr-generate)
        add_link_options(-fprofile-instr-generate)
    else()
        add_compile_options(-fprofile-generate)
        add_link_options(-fprofile-generate)
    endif()
    message(STATUS "PGO: instrumentation enabled — run binary, then rebuild with PGO_USE=ON")
elseif(PGO_USE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(
            -fprofile-instr-use=${PGO_PROFILE_DIR}/default.profdata
            -Wno-profile-instr-unprofiled)
        add_link_options(-fprofile-instr-use=${PGO_PROFILE_DIR}/default.profdata)
    else()
        add_compile_options(-fprofile-use=${PGO_PROFILE_DIR} -fprofile-correction)
        add_link_options(-fprofile-use=${PGO_PROFILE_DIR})
    endif()
    message(STATUS "PGO: optimised build using ${PGO_PROFILE_DIR}/default.profdata")
endif()

add_subdirectory(libs/reference-data)

# ── Unit Tests (Google Test) ───────────────────────────────────────────────────
# Enable:  cmake -DBUILD_TESTS=ON -S . -B build-test
# Run:     ctest --test-dir build-test --output-on-failure
#          ctest --test-dir build-test -R "OrderBook"   ← filter by name
#
# Tests are off by default for the same reason benchmarks are: they pull in
# GTest via FetchContent (network hit on first run) and link extra binaries
# that aren't part of the production build.
option(BUILD_TESTS "Build Google Test unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
add_subdirectory(libs/order-book)
add_subdirectory(libs/position_manager)
add_subdirectory(libs/fix_session)
add_subdirectory(libs/market_data)
add_subdirectory(libs/volatility_surface)
add_subdirectory(libs/risk_engine)
add_subdirectory(libs/metrics_server)
add_subdirectory(src)
add_subdirectory(tools)

# ── Google Benchmark (micro-benchmarks) ────────────────────────────────────────
# Disabled by default — benchmarks are not part of a normal build.
#
# Enable:   cmake -DBUILD_BENCHMARKS=ON -DCMAKE_BUILD_TYPE=Release -S . -B build-bench
# Run:      ./build-bench/bench/bench_order_book
#           ./build/bench/bench_spsc_queue
#           ./build/bench/bench_object_pool
#           ./build/bench/bench_fix_parser
#
# FetchContent downloads google/benchmark at configure time (first run only).
# GIT_SHALLOW=TRUE fetches a single commit instead of the full history.
# BENCHMARK_ENABLE_TESTING=OFF suppresses the benchmark library's own unit tests.
option(BUILD_BENCHMARKS "Build Google Benchmark micro-benchmarks" OFF)

if(BUILD_BENCHMARKS)
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.5
        GIT_SHALLOW    TRUE
    )
    set(BENCHMARK_ENABLE_TESTING      OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL      OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS  OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    add_subdirectory(bench)
endif()
