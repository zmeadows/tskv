# Simple executable test for environment sanity (assert + info)
add_executable(tskv_toolchain_sanity test_smoke.cpp)
set_target_properties(tskv_toolchain_sanity PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
)
add_test(NAME toolchain_sanity COMMAND $<TARGET_FILE:tskv_toolchain_sanity>)

# Smoke tests that invoke the two CLIs' --help
add_test(NAME smoke_help_server COMMAND $<TARGET_FILE:tskv_server> --help)
add_test(NAME smoke_help_client COMMAND $<TARGET_FILE:tskv_client> --help)
