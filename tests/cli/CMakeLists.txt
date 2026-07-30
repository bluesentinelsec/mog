add_executable(mog_cli_test
  cli_test.cpp
  run_test.cpp
)
target_link_libraries(mog_cli_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
target_include_directories(mog_cli_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/http
)
if(WIN32)
  # run_test.cpp drives the in-process LocalHttpServer (winsock).
  target_link_libraries(mog_cli_test PRIVATE ws2_32)
endif()
cppboot_set_project_warnings(mog_cli_test)
gtest_discover_tests(mog_cli_test)
