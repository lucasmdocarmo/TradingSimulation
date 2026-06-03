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

add_subdirectory(libs/reference-data)
add_subdirectory(libs/order-book)
add_subdirectory(libs/position_manager)
add_subdirectory(libs/fix_session)
add_subdirectory(libs/market_data)
add_subdirectory(libs/volatility_surface)
add_subdirectory(libs/risk_engine)
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
