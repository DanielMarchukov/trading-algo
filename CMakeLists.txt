cmake_minimum_required(VERSION 3.28.3)

project(RichOnPaper VERSION 0.0.3)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(SANITIZE_ADDRESS "Enable AddressSanitizer (ASAN)" OFF)
option(SANITIZE_THREAD "Enable ThreadSanitizer (TSAN)" OFF)
option(SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer (UBSAN)" OFF)

if(SANITIZE_ADDRESS AND SANITIZE_THREAD)
    message(FATAL_ERROR "ASAN and TSAN are incompatible — use separate builds.")
endif()

if(MSVC
   AND (SANITIZE_ADDRESS
        OR SANITIZE_THREAD
        OR SANITIZE_UNDEFINED))
    message(
        FATAL_ERROR "Sanitizers are not supported under MSVC in this project. Use GCC or Clang.")
endif()

set(SANITIZER_FLAGS "")
set(SANITIZER_ACTIVE OFF)

if(SANITIZE_ADDRESS)
    list(APPEND SANITIZER_FLAGS "-fsanitize=address")
    set(SANITIZER_ACTIVE ON)
endif()
if(SANITIZE_THREAD)
    list(APPEND SANITIZER_FLAGS "-fsanitize=thread")
    set(SANITIZER_ACTIVE ON)
endif()
if(SANITIZE_UNDEFINED)
    list(APPEND SANITIZER_FLAGS "-fsanitize=undefined")
    set(SANITIZER_ACTIVE ON)
endif()

if(SANITIZER_ACTIVE)
    list(APPEND SANITIZER_FLAGS "-fno-omit-frame-pointer" "-g" "-O1")
    message(STATUS "Sanitizers enabled: ${SANITIZER_FLAGS}")
endif()

if(NOT SANITIZER_ACTIVE)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED)
    if(LTO_SUPPORTED)
        message(STATUS "LTO is supported")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO is not supported")
    endif()
else()
    message(STATUS "LTO disabled (incompatible with sanitizers)")
endif()

if(MSVC)
    add_compile_options("/W4" "/EHsc")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    add_compile_options("-Wall" "-Wextra" "-Wpedantic")
    if(NOT SANITIZER_ACTIVE)
        add_compile_options("-march=native")
    endif()
endif()

find_package(Boost CONFIG REQUIRED COMPONENTS lockfree)
find_package(cppzmq CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)
find_package(TBB CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(msgpack-cxx CONFIG REQUIRED)
find_package(hdr_histogram CONFIG REQUIRED)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)
file(GLOB_RECURSE TESTS tests/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "main.cpp")

include_directories(include)

add_executable(paper_money src/main.cpp ${SOURCES})
target_compile_features(paper_money PUBLIC cxx_std_23)
target_link_libraries(
    paper_money
    PRIVATE Boost::lockfree
            libzmq
            cppzmq
            nlohmann_json::nlohmann_json
            cpr::cpr
            TBB::tbb
            ixwebsocket::ixwebsocket
            msgpack-cxx
            hdr_histogram::hdr_histogram_static)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(paper_money PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(SANITIZER_ACTIVE AND NOT MSVC)
    target_compile_options(paper_money PRIVATE ${SANITIZER_FLAGS})
    target_link_options(paper_money PRIVATE ${SANITIZER_FLAGS})
endif()

enable_testing()
option(ENABLE_COVERAGE "Enable code coverage" OFF)

add_executable(rich_on_tests ${SOURCES} ${TESTS})
target_compile_features(rich_on_tests PUBLIC cxx_std_23)
target_link_libraries(
    rich_on_tests
    PUBLIC Boost::lockfree
           cppzmq
           libzmq
           cpr::cpr
           nlohmann_json::nlohmann_json
           GTest::gtest_main
           TBB::tbb
           ixwebsocket::ixwebsocket
           msgpack-cxx
           hdr_histogram::hdr_histogram_static)

if(SANITIZER_ACTIVE AND NOT MSVC)
    target_compile_options(rich_on_tests PRIVATE ${SANITIZER_FLAGS})
    target_link_options(rich_on_tests PRIVATE ${SANITIZER_FLAGS})
endif()

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
if(SANITIZER_ACTIVE)
    gtest_discover_tests(rich_on_tests DISCOVERY_MODE PRE_TEST)
else()
    gtest_discover_tests(rich_on_tests)
endif()
