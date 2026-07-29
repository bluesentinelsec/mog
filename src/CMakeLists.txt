# Library implementation components.
# Each logical subdirectory owns a CMakeLists.txt that lists sources explicitly.
add_subdirectory(version)
add_subdirectory(http)

# Painfully obvious program entrypoint: src/main.cpp
# Skipped when this project is consumed via add_subdirectory / FetchContent
# unless MOG_BUILD_APP=ON.
if(MOG_BUILD_APP)
  if(NOT MOG_WITH_CLI11)
    message(FATAL_ERROR "MOG_BUILD_APP requires MOG_WITH_CLI11 (cppboot CLI11 stack)")
  endif()
  if(NOT MOG_WITH_SPDLOG)
    message(FATAL_ERROR "MOG_BUILD_APP requires MOG_WITH_SPDLOG (cppboot spdlog stack)")
  endif()
  add_executable(${PROJECT_NAME}_app main.cpp)
  set_target_properties(${PROJECT_NAME}_app PROPERTIES OUTPUT_NAME mog)
  target_link_libraries(${PROJECT_NAME}_app PRIVATE
    ${PROJECT_NAME}_lib
    CLI11::CLI11
    spdlog::spdlog
  )
  if(MOG_WITH_JSON)
    target_link_libraries(${PROJECT_NAME}_app PRIVATE nlohmann_json::nlohmann_json)
  endif()
  cppboot_set_project_warnings(${PROJECT_NAME}_app)
endif()
