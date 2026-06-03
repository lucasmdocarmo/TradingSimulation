add_executable(trading-system
    main.cpp
)

target_link_libraries(trading-system PRIVATE reference-data  fix_session  risk_engine )
