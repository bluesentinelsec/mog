# Tests for the C API. Require the shared library (built with MOG_BUILD_C_SHARED).
if(NOT MOG_BUILD_C_SHARED)
  return()
endif()

# Deterministic C++ test that drives the C API against a local loopback server.
add_executable(mog_c_api_test mog_c_test.cpp)
target_link_libraries(mog_c_api_test
  PRIVATE
    ${PROJECT_NAME}_c
    GTest::gtest_main
)
target_include_directories(mog_c_api_test
  PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/http   # test_support/local_http_server.hpp
)
if(WIN32)
  target_link_libraries(mog_c_api_test PRIVATE ws2_32)
endif()
cppboot_set_project_warnings(mog_c_api_test)
gtest_discover_tests(mog_c_api_test)

# Pure-C compile + link check: proves the header is valid C and symbols resolve.
add_executable(mog_c_compile_check compile_check.c)
target_link_libraries(mog_c_compile_check PRIVATE ${PROJECT_NAME}_c)
target_include_directories(mog_c_compile_check PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(mog_c_compile_check PROPERTIES C_STANDARD 99)
add_test(NAME mog_c_compile_check COMMAND mog_c_compile_check)

# ctypes smoke test: load the shared library from Python and perform a request.
# Skipped gracefully when a Python 3 interpreter is not available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME mog_c_ctypes
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/ctypes_smoke.py
      $<TARGET_FILE:${PROJECT_NAME}_c>
  )
else()
  message(STATUS "Python3 not found: skipping mog_c_ctypes smoke test")
endif()
