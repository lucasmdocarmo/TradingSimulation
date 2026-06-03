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
