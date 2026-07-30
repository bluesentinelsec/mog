set(MOG_HTTP_TEST_SOURCES
  url_test.cpp
  backend_test.cpp
  client_test.cpp
  conformance_test.cpp
  keep_alive_test.cpp
  transport_test.cpp
  content_encoding_test.cpp
  ca_store_test.cpp
  streaming_test.cpp
)
if(MOG_WITH_JSON)
  list(APPEND MOG_HTTP_TEST_SOURCES json_test.cpp)
endif()
if(MOG_WITH_SPDLOG)
  list(APPEND MOG_HTTP_TEST_SOURCES log_test.cpp)
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
    ${CMAKE_CURRENT_SOURCE_DIR}
)
if(WIN32)
  target_link_libraries(mog_http_test PRIVATE ws2_32)
endif()
# miniz / mbedTLS are PRIVATE on the static lib; unit tests that call them need links.
target_link_libraries(mog_http_test PRIVATE miniz mbedtls mbedx509 mbedcrypto)
cppboot_set_project_warnings(mog_http_test)
gtest_discover_tests(mog_http_test)
