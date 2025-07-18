cmake_minimum_required(VERSION 3.28.3)

project(RichOnPaper VERSION 0.0.3)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

find_package(Boost REQUIRED)
find_package(QuantLib CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
find_package(cpr REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)

if(MSVC)
    add_compile_options("/W4" "/EHsc")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    add_compile_options("-Wall" "-Wextra" "-Wpedantic" "-march=native")
endif()

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)
file(GLOB_RECURSE TESTS tests/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "main.cpp")

include_directories(include)

add_executable(hello src/main.cpp ${SOURCES})
target_link_libraries(hello PRIVATE
    Boost::boost
    QuantLib::QuantLib
    libzmq
    cppzmq
    nlohmann_json::nlohmann_json
    cpr::cpr
    unofficial::uwebsockets::uwebsockets
)

# ===================================================================
# Testing Configuration
# ===================================================================
enable_testing()
option(ENABLE_COVERAGE "Enable code coverage" OFF)

add_executable(rich_on_tests ${SOURCES} ${TESTS})
target_link_libraries(rich_on_tests PUBLIC
    Boost::boost
    QuantLib::QuantLib
    cppzmq
    libzmq
    cpr::cpr
    nlohmann_json::nlohmann_json
    GTest::gtest_main
)

if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Using GCC for code coverage")
        target_compile_options(rich_on_tests INTERFACE --coverage)
        target_link_options(rich_on_tests INTERFACE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(STATUS "Using Clang for code coverage")
        target_compile_options(rich_on_tests INTERFACE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(rich_on_tests INTERFACE -fprofile-instr-generate)
    else()
        message(WARNING "Code coverage is not supported for the current compiler.")
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(rich_on_tests)

