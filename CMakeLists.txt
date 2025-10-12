cmake_minimum_required(VERSION 3.28.3)

project(RichOnPaper VERSION 0.0.3)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED)
if(LTO_SUPPORTED)
    message(STATUS "LTO is supported")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "LTO is not supported")
endif()

if(MSVC)
    add_compile_options("/W4" "/EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    add_compile_options("-Wall" "-Wextra" "-Wpedantic" "-march=native")
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS lockfree)
find_package(cppzmq CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)
find_package(unofficial-uwebsockets CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)
file(GLOB_RECURSE TESTS tests/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "main.cpp")

include_directories(include)

add_executable(paper_money src/main.cpp ${SOURCES})
target_link_libraries(
    paper_money
    PRIVATE Boost::lockfree
            libzmq
            cppzmq
            nlohmann_json::nlohmann_json
            cpr::cpr
            unofficial::uwebsockets::uwebsockets
            TBB::tbb)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(paper_money PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ===================================================================
# Testing Configuration
# ===================================================================
enable_testing()
option(ENABLE_COVERAGE "Enable code coverage" OFF)

add_executable(rich_on_tests ${SOURCES} ${TESTS})
target_link_libraries(
    rich_on_tests
    PUBLIC Boost::lockfree
           cppzmq
           libzmq
           cpr::cpr
           nlohmann_json::nlohmann_json
           GTest::gtest_main
           TBB::tbb)

if(ENABLE_COVERAGE)
    message(STATUS "Code coverage enabled")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(STATUS "Using GCC for code coverage")
        target_compile_options(rich_on_tests PRIVATE --coverage -fprofile-update=atomic)
        target_link_options(rich_on_tests PRIVATE --coverage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(STATUS "Using Clang/AppleClang for code coverage")
        target_compile_options(rich_on_tests PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(rich_on_tests PRIVATE -fprofile-instr-generate)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "Using MSVC - coverage requires external tools (OpenCppCoverage)")
        target_compile_options(rich_on_tests PRIVATE /Zi /DEBUG:FULL)
        target_link_options(rich_on_tests PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    else()
        message(
            WARNING
                "Code coverage is not supported for the current compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

include(GoogleTest)
gtest_discover_tests(rich_on_tests)
