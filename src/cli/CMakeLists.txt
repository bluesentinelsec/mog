# CLI argument mapping (testable). CLI11 parse is enabled when MOG_WITH_CLI11.
target_sources(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cli.cpp
)

if(MOG_WITH_CLI11)
  target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:CLI11::CLI11>
  )
  target_compile_definitions(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:MOG_HAS_CLI11=1>
  )
endif()
