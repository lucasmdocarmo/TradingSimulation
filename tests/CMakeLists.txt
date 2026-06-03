cmake_minimum_required(VERSION 3.20)

# ── Google Test ────────────────────────────────────────────────────────────────
# Prefer the system-installed GTest (e.g. `brew install googletest` on macOS).
# Fall back to FetchContent only if no system installation is found.
# This avoids the header/source version mismatch that occurs when FetchContent
# downloads an older GTest while the compiler finds a newer one in the system
# include path first.
#
# WHY GTEST?
# Google Test is the de-facto standard in C++ systems codebases. Individual
# tests register as separate CTest entries via gtest_discover_tests(), so you
# can filter, repeat, and parallelise them independently:
#
#   ctest --test-dir build-test --output-on-failure
#   ctest --test-dir build-test -R "OrderBook"     # run only OrderBook tests
#   ctest --test-dir build-test -j4                # run in parallel
find_package(GTest QUIET HINTS /opt/homebrew /usr/local)

if(NOT GTest_FOUND)
    message(STATUS "GTest not found via find_package — falling back to FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST          OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
else()
    message(STATUS "GTest ${GTest_VERSION} found at ${GTEST_INCLUDE_DIRS}")
    # gtest_discover_tests requires this module even when not using FetchContent.
    include(GoogleTest)
endif()

include(GoogleTest)

# ── Helper function ────────────────────────────────────────────────────────────
# -O2 -g: optimise enough that the code behaves like a real build (catches
# bugs that only appear at -O2, e.g. UB exposed by missed init), while keeping
# debug symbols so GDB/LLDB can show source lines on failures.
function(add_unit_test name source)
    add_executable(${name} ${source})
    target_compile_options(${name} PRIVATE -O2 -g)
    target_link_libraries(${name} PRIVATE
        GTest::gtest_main   # provides main(); we don't write one
        GTest::gmock        # matchers and mock framework
        ${ARGN}             # project library targets passed as extra args
    )
    # Registers each TEST / TEST_F as an individual CTest item at configure time.
    # Allows `ctest -R <pattern>` filtering and per-test pass/fail reporting.
    gtest_discover_tests(${name})
endfunction()

# ── Test targets ───────────────────────────────────────────────────────────────
add_unit_test(test_order_book        unit/test_order_book.cpp        order_book)
add_unit_test(test_fix_parser        unit/test_fix_parser.cpp        fix_session)
add_unit_test(test_market_dispatcher unit/test_market_dispatcher.cpp market_data)
add_unit_test(test_latency_tracker   unit/test_latency_tracker.cpp)
add_unit_test(test_spsc_queue        unit/test_spsc_queue.cpp)
add_unit_test(test_position_manager  unit/test_position_manager.cpp  position_manager reference-data)
