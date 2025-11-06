cmake_minimum_required(VERSION 3.28)
project(tskv
  VERSION 0.1.0
  LANGUAGES CXX)

# Build metadata
execute_process(
  COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=12 HEAD
  RESULT_VARIABLE TSKV_GIT_RESULT
  OUTPUT_VARIABLE TSKV_GIT_SHA
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT TSKV_GIT_RESULT EQUAL 0 OR TSKV_GIT_SHA STREQUAL "")
  set(TSKV_GIT_SHA "nogit")
endif()

# UTC timestamp
string(TIMESTAMP TSKV_BUILD_UTC "%Y-%m-%dT%H:%M:%SZ" UTC)

# Expose for subdirs
set(TSKV_PROJECT_VERSION
    "${PROJECT_VERSION}"
    CACHE STRING "" FORCE)
set(TSKV_GIT_SHA
    "${TSKV_GIT_SHA}"
    CACHE STRING "" FORCE)
set(TSKV_BUILD_UTC
    "${TSKV_BUILD_UTC}"
    CACHE STRING "" FORCE)
set(TSKV_COMPILER_ID
    "${CMAKE_CXX_COMPILER_ID}"
    CACHE STRING "" FORCE)
set(TSKV_COMPILER_VER
    "${CMAKE_CXX_COMPILER_VERSION}"
    CACHE STRING "" FORCE)

include(cmake/ToolchainChecks.cmake)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TSKV_USE_ABSAN_UBSAN "Enable ABSAN/UBSAN" OFF)
option(TSKV_USE_TSAN        "Enable TSAN" OFF)

if(TSKV_USE_ABSAN_UBSAN AND TSKV_USE_TSAN)
  message(FATAL_ERROR "TSKV_USE_ABSAN_UBSAN and TSKV_USE_TSAN cannot both be ON")
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O3 -g -fno-omit-frame-pointer -pthread")
add_link_options(-pthread)

if(TSKV_USE_ABSAN_UBSAN)
  message(STATUS "SANITIZERS: ABSAN + UBSAN enabled")
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
elseif(TSKV_USE_TSAN)
  message(STATUS "SANITIZERS: TSAN enabled")
  add_compile_options(-fsanitize=thread)
  add_link_options(-fsanitize=thread)
else()
  message(STATUS "SANITIZERS: OFF")
endif()

# buildinfo.txt
file(
  WRITE "${CMAKE_BINARY_DIR}/buildinfo.txt"
  "CMAKE_VERSION=${CMAKE_VERSION}\n"
  "CXX=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
  "CXX_STANDARD=${CMAKE_CXX_STANDARD}\n"
  "BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
  "TSKV_USE_ABSAN_UBSAN=${TSKV_USE_ABSAN_UBSAN}\n"
  "TSKV_USE_TSAN=${TSKV_USE_TSAN}\n"
  "CXX_FLAGS=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}\n")

# Make a root-level symlink to compile_commands.json for editor tooling
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(
    copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
    COMMENT "Symlinking compile_commands.json to project root"
    VERBATIM)
endif()

add_subdirectory(src/common)
add_subdirectory(src/net)
add_subdirectory(src/storage)
add_subdirectory(cmd)
add_subdirectory(tests)
