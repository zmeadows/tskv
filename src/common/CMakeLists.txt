add_library(tskv_common)

target_sources(
  tskv_common
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         logging.ixx
         files.ixx
         enum_traits.ixx
  PUBLIC FILE_SET HEADERS FILES defer.hpp)
