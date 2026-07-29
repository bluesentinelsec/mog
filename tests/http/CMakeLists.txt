set(MOG_HTTP_TEST_SOURCES
  url_test.cpp
  backend_test.cpp
  client_test.cpp
)
if(MOG_WITH_JSON)
  list(APPEND MOG_HTTP_TEST_SOURCES json_test.cpp)
endif()

add_executable(mog_http_test ${MOG_HTTP_TEST_SOURCES})
target_link_libraries(mog_http_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
target_include_directories(mog_http_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
if(WIN32)
  target_link_libraries(mog_http_test PRIVATE ws2_32)
endif()
cppboot_set_project_warnings(mog_http_test)
gtest_discover_tests(mog_http_test)
