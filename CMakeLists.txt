cmake_minimum_required(VERSION 3.28.3)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(DEFAULT_BUILD_TYPE "Debug")
  message(
    STATUS
      "Set the build type to `${default_build_type}` as none was specified.")
  set(CMAKE_BUILD_TYPE
      ${DEFAULT_BUILD_TYPE}
      CACHE STRING "Chooce the build type." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release"
                                               "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "TradingAlgo Build Type: ${CMAKE_BUILD_TYPE}")

# Set the version for myProject
set(MAJOR_V 0)
set(MINOR_V 1)
set(PATCH_V 0)
set(PROJECT_VERSION "${MAJOR_V}.${MINOR_V}.${PATCH_V}")
message(STATUS "${PROJECT_NAME} version: ${PROJECT_VERSION}")
# target_include_directories(TradingAlgo PUBLIC ${PROJECT_BINARY_DIR})

project(TradingAlgo VERSION ${PROJECT_VERSION})

set(_SOURCES
    src
    CACHE INTERNAL "")
add_subdirectory(${SOURCES})
add_executable(hello ${SOURCES}/main.cpp)
