# Browser-runtime contract for the Emscripten Fetch backend.
add_executable(mog_web_test web_test.cpp)
target_link_libraries(mog_web_test
  PRIVATE
    ${PROJECT_NAME}_lib
    GTest::gtest_main
)
target_include_directories(mog_web_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(mog_web_test PROPERTIES SUFFIX ".html")
target_link_options(mog_web_test
  PRIVATE
    "--emrun"
    "SHELL:-sEXIT_RUNTIME=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
)
add_custom_command(TARGET mog_web_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/web_fixture.txt
    $<TARGET_FILE_DIR:mog_web_test>/web_fixture.txt
  VERBATIM
)
cppboot_set_project_warnings(mog_web_test)
