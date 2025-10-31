add_library(tskv_kv)
target_sources(
  tskv_kv
  PUBLIC FILE_SET CXX_MODULES FILES engine.ixx)
