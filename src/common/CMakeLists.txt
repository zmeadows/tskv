add_library(tskv_common)

target_sources(tskv_common
  PUBLIC FILE_SET CXX_MODULES
  FILES buffer.ixx
        enum_traits.ixx
        files.ixx
        key_array.ixx
        key_set.ixx
        logging.ixx
        metrics.ixx
        string_literal.ixx
)

target_sources(tskv_common
  PUBLIC FILE_SET HEADERS
  BASE_DIRS "${TSKV_PUBLIC_INCLUDE_DIR}"
  FILES "${TSKV_PUBLIC_INCLUDE_DIR}/tskv/common/logging.hpp"
        "${TSKV_PUBLIC_INCLUDE_DIR}/tskv/common/attributes.hpp"
        "${TSKV_PUBLIC_INCLUDE_DIR}/tskv/common/defer.hpp"
)
