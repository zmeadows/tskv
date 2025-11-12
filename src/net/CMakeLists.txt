add_library(tskv_net)
target_sources(
  tskv_net
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         channel.ixx
         reactor.ixx
         server.ixx
         socket.ixx
         utils.ixx
)

target_link_libraries(
  tskv_net
  PRIVATE tskv_common)
