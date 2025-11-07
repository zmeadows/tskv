add_library(tskv_common)

target_sources(
  tskv_common
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         buffer.ixx
         enum_traits.ixx
         files.ixx
         key_array.ixx
         key_set.ixx
         logging.ixx
         metrics.ixx
         string_literal.ixx
  PUBLIC FILE_SET HEADERS FILES defer.hpp macro_utils.hpp)
