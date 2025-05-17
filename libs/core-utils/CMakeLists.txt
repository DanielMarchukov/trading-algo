# libs/market-data/CMakeLists.txt
project(CoreUtils LANGUAGES CXX)

add_library(core-utils STATIC src/)

target_include_directories(core-utils
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
