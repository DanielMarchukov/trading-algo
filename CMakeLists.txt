# cmake-lint disable=C0301
cmake_minimum_required(VERSION 4.0.0)

project(RichOnPaper VERSION 0.0.2)

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

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FindQuantLib)
include(FindPython3)
find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED)
find_package(QuantLib REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development.Embed)
find_package(cppzmq REQUIRED)

# include(FetchContent) FetchContent_Declare( googletest URL
# https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
# ) FetchContent_MakeAvailable(googletest)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")
add_compile_options("-Wall" "-Wextra" "-Wpedantic" "-march=native")

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)

include_directories(include)
# add_library(core STATIC ${SOURCES}) later to be used as core engine

add_executable(hello ${SOURCES} ${HEADERS})
target_link_libraries(hello PUBLIC Boost::boost QuantLib libzmq Python3::Python)

# add_subdirectory() - build recursively.
