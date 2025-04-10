cmake_minimum_required(VERSION 4.0.0)

project(TradingAlgo VERSION 0.0.1)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_DEPS ON)
set(QL_BUILD_EXAMPLES
    OFF
    CACHE BOOL "" FORCE)
set(QL_BUILD_TEST_SUITE
    OFF
    CACHE BOOL "" FORCE)
set(FETCHCONTENT_QUIET OFF)
set(Boost_DEBUG ON)

if(NOT CMAKE_BUILD_TYPE)
  set(DEFAULT_BUILD_TYPE "Debug")
  set(CMAKE_BUILD_TYPE
      ${DEFAULT_BUILD_TYPE}
      CACHE STRING "Choose the build type." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                               "MinSizeRel" "RelWithDebInfo")
endif()

find_package(PkgConfig REQUIRED)
find_package(Boost)
find_package(QuantLib)

# list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake") QuantLib
if(NOT Boost_FOUND)
  FetchContent_Declare(
    Boost
    GIT_REPOSITORY https://github.com/boostorg/boost.git
    GIT_TAG boost-1.88.0.beta1)
  FetchContent_MakeAvailable(Boost)
endif()

if(NOT QuantLib_FOUND)
  FetchContent_Declare(
    quantlib
    GIT_REPOSITORY https://github.com/lballabio/QuantLib.git
    GIT_TAG v1.37)
  FetchContent_MakeAvailable(quantlib)
endif()

set(_SOURCES
    src
    CACHE INTERNAL "")
add_subdirectory(${_SOURCES})

add_executable(hello ${_SOURCES}/main.cpp)
target_link_libraries(hello PUBLIC Boost::boost QuantLib)
target_include_directories(hello PUBLIC ${Boost_INCLUDE_DIRS}
                                        ${quantlib_INCLUDE_DIRS})
