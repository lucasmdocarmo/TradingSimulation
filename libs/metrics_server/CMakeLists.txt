add_library(metrics_server STATIC
    src/metrics_server.cpp
)

target_include_directories(metrics_server PUBLIC include)

# Beast and Asio are fully header-only. Boost::headers provides include paths.
# Since Boost 1.75, Boost.System is also header-only (no compiled lib needed).
target_link_libraries(metrics_server PUBLIC Boost::headers)
