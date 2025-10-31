cmake_minimum_required(VERSION 3.28)
project(tskv LANGUAGES CXX)

# Step 0 is Clang-only for smoother C++23 modules + sanitizers.
include(cmake/ToolchainChecks.cmake)

# Global language setup
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to RelWithDebInfo (O3 + g) unless user overrides.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json and later symlink/copy it to repo root
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TSKV_USE_SANITIZERS "Enable Address/Undefined sanitizers" ON)

# Strict warnings globally
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# Baseline flags for RelWithDebInfo as per acceptance
string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -O3 -g -fno-omit-frame-pointer -pthread")
# Link option for pthread (some toolchains need link flag explicitly)
add_link_options(-pthread)

include(cmake/Sanitizers.cmake)

# Build info provenance
file(WRITE "${CMAKE_BINARY_DIR}/buildinfo.txt"
  "CMAKE_VERSION=${CMAKE_VERSION}\n"
  "CXX=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n"
  "CXX_STANDARD=${CMAKE_CXX_STANDARD}\n"
  "BUILD_TYPE=${CMAKE_BUILD_TYPE}\n"
  "TSKV_USE_SANITIZERS=${TSKV_USE_SANITIZERS}\n"
  "CXX_FLAGS=${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}\n"
)

# Make a root-level symlink (Linux) to compile_commands.json for editor tooling
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_BINARY_DIR}/compile_commands.json"
      "${CMAKE_SOURCE_DIR}/compile_commands.json"
    COMMENT "Symlinking compile_commands.json to project root"
    VERBATIM
  )
endif()

enable_testing()

add_subdirectory(src/common)
add_subdirectory(src/net)
add_subdirectory(src/kv)
add_subdirectory(cmd)
add_subdirectory(tests)
