function(add_cli_test NAME TARGET)
  # Usage:
  # add_cli_test(name target
  #   ARGS --foo bar
  #   PASS "regex1;regex2"
  #   FAIL "regex_bad"
  #   EXPECT_FAIL            # if a non-zero exit is the *expected* success condition
  #   ENV "KEY=VALUE;X=1"
  #   TIMEOUT 10
  #   WORKDIR /path
  #   LABELS "cli;server"
  # )
  cmake_parse_arguments(
    CT
    "EXPECT_FAIL"
    "TIMEOUT;WORKDIR"
    "ARGS;PASS;FAIL;ENV;LABELS"
    ${ARGN})

  add_test(NAME ${NAME} COMMAND $<TARGET_FILE:${TARGET}> ${CT_ARGS})

  if(CT_PASS)
    # All PASS patterns must match; separate with semicolons.
    set_tests_properties(${NAME} PROPERTIES PASS_REGULAR_EXPRESSION "${CT_PASS}")
  endif()

  if(CT_FAIL)
    # If any FAIL pattern appears in output, the test fails.
    set_tests_properties(${NAME} PROPERTIES FAIL_REGULAR_EXPRESSION "${CT_FAIL}")
  endif()

  if(CT_EXPECT_FAIL)
    # Tells CTest a non-zero exit is expected and should be treated as pass.
    set_tests_properties(${NAME} PROPERTIES WILL_FAIL TRUE)
  endif()

  if(CT_ENV)
    set_tests_properties(${NAME} PROPERTIES ENVIRONMENT "${CT_ENV}")
  endif()

  if(CT_TIMEOUT)
    set_tests_properties(${NAME} PROPERTIES TIMEOUT ${CT_TIMEOUT})
  else()
    set_tests_properties(${NAME} PROPERTIES TIMEOUT 15)
  endif()

  if(CT_WORKDIR)
    set_tests_properties(${NAME} PROPERTIES WORKING_DIRECTORY "${CT_WORKDIR}")
  endif()

  if(CT_LABELS)
    set_tests_properties(${NAME} PROPERTIES LABELS "${CT_LABELS}")
  endif()
endfunction()
