# Embedded HTTP server tests. Driven by mog's own client on a loopback port,
# plus raw TcpSocket cases for wire-level behavior.
add_executable(mog_server_test server_test.cpp)
target_link_libraries(mog_server_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
target_include_directories(mog_server_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src   # detail/socket.hpp for raw-socket cases
)
if(WIN32)
  target_link_libraries(mog_server_test PRIVATE ws2_32)
endif()
# miniz / mbedTLS are PRIVATE on the static lib; unit tests that pull them need links.
target_link_libraries(mog_server_test PRIVATE miniz mbedtls mbedx509 mbedcrypto)
cppboot_set_project_warnings(mog_server_test)
gtest_discover_tests(mog_server_test DISCOVERY_TIMEOUT ${MOG_GTEST_DISCOVERY_TIMEOUT})
