# Portable runtime shared-library loader (dlopen / LoadLibrary).
target_sources(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dynload.cpp
)

if(UNIX AND NOT APPLE)
  # libdl for dlopen/dlsym on Linux; Apple ships it in libSystem.
  target_link_libraries(${PROJECT_NAME}_lib PRIVATE ${CMAKE_DL_LIBS})
elseif(APPLE)
  target_link_libraries(${PROJECT_NAME}_lib PRIVATE ${CMAKE_DL_LIBS})
endif()
