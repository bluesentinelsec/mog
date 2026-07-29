add_executable(mog_cli_test
  cli_test.cpp
  run_test.cpp
)
target_link_libraries(mog_cli_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
if(WIN32)
  # mkstemp path uses /tmp on Unix; Windows branch needs no extra libs for CLI tests.
endif()
cppboot_set_project_warnings(mog_cli_test)
gtest_discover_tests(mog_cli_test)
