add_library(tskv_net)
target_sources(
  tskv_net
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         socket.ixx
         utils.ixx)
