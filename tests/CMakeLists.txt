enable_testing()

add_test(NAME smoke_help_server COMMAND $<TARGET_FILE:tskv_server> --help)
add_test(NAME smoke_help_client COMMAND $<TARGET_FILE:tskv_client> --help)
