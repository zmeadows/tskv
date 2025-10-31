add_executable(tskv_server server.cpp)
set_target_properties(tskv_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cmd"
                                             OUTPUT_NAME "server")

add_executable(tskv_client client.cpp)
set_target_properties(tskv_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/cmd"
                                             OUTPUT_NAME "client")

# Link to libraries as they grow; for now just keep linkage minimal.
target_link_libraries(
  tskv_server
  PRIVATE tskv_common)
target_link_libraries(
  tskv_client
  PRIVATE)
