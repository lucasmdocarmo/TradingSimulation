add_library(risk_engine
    src/risk_engine.cpp
)

target_include_directories( risk_engine PUBLIC include)
target_link_libraries(risk_engine  PUBLIC position_manager volatility_surface market_data)
