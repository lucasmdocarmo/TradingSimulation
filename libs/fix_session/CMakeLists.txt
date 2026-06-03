add_library(fix_session
    src/fix_session_reader.cpp
)

target_include_directories(fix_session PUBLIC include)
target_link_libraries(fix_session PUBLIC order_book)
