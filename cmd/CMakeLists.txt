#
# cmd/CMakeLists.txt (after) — ensure BMI order for cmd.args by linking its providers
#

add_executable(tskv_server server.cpp)
set_target_properties(tskv_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cmd"
                                             OUTPUT_NAME "server")

add_executable(tskv_client client.cpp)
set_target_properties(tskv_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cmd"
                                             OUTPUT_NAME "client")

set(TSKV_VERSION_IXX "${CMAKE_CURRENT_BINARY_DIR}/version.ixx")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.ixx.in" "${TSKV_VERSION_IXX}" @ONLY)

add_library(tskv_cmd)

# Shared CLI modules (args.ixx + optional peers live next to the mains)
target_sources(
  tskv_cmd
  PUBLIC FILE_SET CXX_MODULES FILES args.ixx
         # version.ixx     # (optional) list any additional cmd.* modules here
  PUBLIC FILE_SET HEADERS FILES macros.hpp # (optional) makes the header visible to IDEs/exports
)

# Register the generated version.ixx module
target_sources(
  tskv_cmd
  PUBLIC FILE_SET
         CXX_MODULES
         BASE_DIRS
         "${CMAKE_CURRENT_BINARY_DIR}"
         FILES
         "${TSKV_VERSION_IXX}")

target_link_libraries(
  tskv_cmd
  PRIVATE tskv_common tskv_storage)

target_link_libraries(
  tskv_server
  PRIVATE tskv_cmd tskv_common tskv_storage)

target_link_libraries(
  tskv_client
  PRIVATE tskv_cmd tskv_common tskv_storage)
