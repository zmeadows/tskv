cmake_minimum_required(VERSION 3.28)
project(tskv LANGUAGES CXX)

include(cmake/ToolchainChecks.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE
      RelWithDebInfo
      CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TSKV_USE_SANITIZERS "Enable Address/Undefined sanitizers" ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O3 -g -fno-omit-frame-pointer -pthread")
add_link_options(-pthread)

include(cmake/Sanitizers.cmake)

# buildinfo.txt
file(
  WRITE "${CMAKE_BINARY_DIR}/buildinfo.txt"
  "CMAKE_VERSION=${CMAKE_VERSION}\n"
  "CXX=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
  "CXX_STANDARD=${CMAKE_CXX_STANDARD}\n"
  "BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
  "TSKV_USE_SANITIZERS=${TSKV_USE_SANITIZERS}\n"
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

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/net)
add_subdirectory(src/kv)
add_subdirectory(cmd)
add_subdirectory(tests)
