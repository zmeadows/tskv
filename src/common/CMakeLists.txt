add_library(tskv_common)

target_sources(
  tskv_common
  PUBLIC FILE_SET
         CXX_MODULES
         FILES
         enum_traits.ixx
         files.ixx
         logging.ixx
         metrics.ixx
         string_literal.ixx
         key_set.ixx
         key_array.ixx
  PUBLIC FILE_SET HEADERS FILES defer.hpp macro_utils.hpp)
