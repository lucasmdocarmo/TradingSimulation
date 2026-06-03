add_library(market_data
    src/market_dispatcher.cpp
    src/udp_feed_subscriber.cpp
)

target_include_directories(market_data PUBLIC include)
