add_library(tskv_common)
target_sources(tskv_common
  PUBLIC
    FILE_SET CXX_MODULES FILES
      logging.ixx
)
