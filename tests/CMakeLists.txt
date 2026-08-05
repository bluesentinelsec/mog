# Unit tests — one subdirectory per component under test.
if(EMSCRIPTEN)
  add_subdirectory(web)
  return()
endif()

add_subdirectory(version)
add_subdirectory(dynload)
add_subdirectory(http)
add_subdirectory(cli)
add_subdirectory(c_api)
add_subdirectory(server)
