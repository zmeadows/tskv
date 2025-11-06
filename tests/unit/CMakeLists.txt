add_executable(tskv_unit_tests
  unit_main.cpp
  common/test_key_array.cpp
  common/test_key_set.cpp
  common/test_metrics.cpp
  common/test_string_literal.cpp
  net/test_utils.cpp
)

set(TSKV_DOCTEST_DIR "${CMAKE_SOURCE_DIR}/tests/third_party/doctest")

target_include_directories(tskv_unit_tests
  SYSTEM PRIVATE
    "${TSKV_DOCTEST_DIR}"
)

target_link_libraries(tskv_unit_tests
  PRIVATE
    tskv_common
    tskv_storage
    tskv_net
    tskv_cmd
)

doctest_discover_tests(tskv_unit_tests
  TEST_PREFIX "unit."
  ADD_LABELS  1
  TEST_LIST   tskv_unit_cases
  PROPERTIES  LABELS "unit"
)
