add_executable(mog_dynload_test dynload_test.cpp)
target_link_libraries(mog_dynload_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
cppboot_set_project_warnings(mog_dynload_test)
gtest_discover_tests(mog_dynload_test)
