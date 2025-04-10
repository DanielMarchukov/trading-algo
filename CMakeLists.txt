cmake_minimum_required(VERSION 4.0.0)

project(TradingAlgo VERSION 0.0.1)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(DEFAULT_BUILD_TYPE "Debug")
  set(CMAKE_BUILD_TYPE
      ${DEFAULT_BUILD_TYPE}
      CACHE STRING "Choose the build type." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                               "MinSizeRel" "RelWithDebInfo")
endif()

find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED)
find_package(QuantLib)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(_SOURCES
    src
    CACHE INTERNAL "")
add_subdirectory(${_SOURCES})

add_executable(hello ${_SOURCES}/main.cpp)
target_link_libraries(hello PUBLIC Boost::boost QuantLib)
