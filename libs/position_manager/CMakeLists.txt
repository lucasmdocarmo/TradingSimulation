add_library(position_manager
    src/position_manager.cpp
)

target_include_directories(position_manager PUBLIC include)
target_link_libraries(position_manager PUBLIC order_book)
