add_library(tskv_storage)

target_sources(
  tskv_storage
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         engine.ixx
         wal.ixx)

target_link_libraries(
  tskv_storage
  PRIVATE tskv_common)
