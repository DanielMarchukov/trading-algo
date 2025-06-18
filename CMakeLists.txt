cmake_minimum_required(VERSION 4.0.0)

project(RichOnPaper VERSION 0.0.2)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(INSTALL_GTEST OFF)
set(gtest_force_shared_crt ON)


if(NOT CMAKE_BUILD_TYPE)
    set(DEFAULT_BUILD_TYPE "Debug")
    set(CMAKE_BUILD_TYPE ${DEFAULT_BUILD_TYPE} CACHE STRING "Build Type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindQuantLib)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED)
find_package(QuantLib REQUIRED)
find_package(cppzmq REQUIRED)

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0)
FetchContent_MakeAvailable(googletest)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")
add_compile_options("-Wall" "-Wextra" "-Wpedantic" "-march=native")

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)
file(GLOB_RECURSE TESTS tests/*.cpp)
list(FILTER SOURCES EXCLUDE REGEX "main.cpp")

include_directories(include)
# add_library(core STATIC ${SOURCES}) later to be used as core engine

add_executable(hello ${SOURCES} ${HEADERS} src/main.cpp)
target_link_libraries(hello PUBLIC Boost::boost QuantLib libzmq)

# add_subdirectory() - build recursively.

# ===================================================================
# Testing Configuration
# ===================================================================
enable_testing()
option(ENABLE_COVERAGE "Enable code coverage" OFF)
add_executable(rich_on_tests ${SOURCES} ${TESTS})
target_link_libraries(rich_on_tests PUBLIC
    Boost::boost
    QuantLib
    libzmq
    GTest::gtest_main
)
if(ENABLE_COVERAGE)
    # None of this is actually working, the coverage does not get generated.
    # TODO: Needs investigation.
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
