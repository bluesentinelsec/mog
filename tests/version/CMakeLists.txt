add_executable(mog_version_test
  version_test.cpp
)
target_link_libraries(mog_version_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
cppboot_set_project_warnings(mog_version_test)
gtest_discover_tests(mog_version_test DISCOVERY_TIMEOUT ${MOG_GTEST_DISCOVERY_TIMEOUT})
