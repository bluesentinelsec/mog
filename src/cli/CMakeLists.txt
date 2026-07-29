# CLI component — partitioned by responsibility (SRP):
#   prepare.cpp  Args → Prepared / Options
#   output.cpp   response presentation + exit codes
#   run.cpp      use-case orchestration
#   parse.cpp    CLI11 argv front-end (optional)
target_sources(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/prepare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/output.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/run.cpp
)

if(MOG_WITH_CLI11)
  target_sources(${PROJECT_NAME}_lib
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/parse.cpp
  )
  target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:CLI11::CLI11>
  )
  target_compile_definitions(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:MOG_HAS_CLI11=1>
  )
endif()
