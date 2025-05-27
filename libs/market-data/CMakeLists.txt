# libs/market-data/CMakeLists.txt
project(MarketData LANGUAGES CXX)

add_library(market-data STATIC src/alpaca_adapter.cpp src/binance_ws.cpp)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindQuantLib)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED)
find_package(QuantLib REQUIRED)

target_include_directories(market-data
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(market-data PRIVATE Boost::boost core-utils QuantLib)
