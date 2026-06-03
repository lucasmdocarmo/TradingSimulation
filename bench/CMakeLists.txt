# ── Benchmark Targets ─────────────────────────────────────────────────────────
# Each file ends with BENCHMARK_MAIN() — its own main().
# Link against benchmark::benchmark (the framework only).
# benchmark::benchmark_main would define a second main() and cause a link error.
#
# -O3 -march=native: match the flags you'd use in a production build so that
# benchmark times reflect real instruction throughput, not debug overhead.

cmake_minimum_required(VERSION 3.20)

function(add_bench name source)
    add_executable(${name} ${source})
    target_compile_options(${name} PRIVATE -O3 -march=native)
    target_link_libraries(${name} PRIVATE benchmark::benchmark ${ARGN})
endfunction()

# bench_order_book: acceptNewOrder, matchOrders, sweep, multi-symbol
add_bench(bench_order_book  bench_order_book.cpp  order_book)

# bench_spsc_queue: round-trip, push-only, two-thread cross-core, capacity sweep
# Links market_data for tick.h include path (PUBLIC target_include_directories)
add_bench(bench_spsc_queue  bench_spsc_queue.cpp  market_data)

# bench_object_pool: pool vs heap, batch alloc, near-exhaustion
# Links order_book for order.h include path
add_bench(bench_object_pool bench_object_pool.cpp order_book)

# bench_fix_parser: single line, round-robin, in-memory file replay
add_bench(bench_fix_parser  bench_fix_parser.cpp  fix_session)
