# Library implementation components.
# Each logical subdirectory owns a CMakeLists.txt that lists sources explicitly.
add_subdirectory(version)
add_subdirectory(http)

# Painfully obvious program entrypoint: src/main.cpp
# Skipped when this project is consumed via add_subdirectory / FetchContent
# unless MOG_BUILD_APP=ON.
if(MOG_BUILD_APP)
  add_executable(${PROJECT_NAME}_app main.cpp)
  set_target_properties(${PROJECT_NAME}_app PROPERTIES OUTPUT_NAME mog)
  target_link_libraries(${PROJECT_NAME}_app PRIVATE ${PROJECT_NAME}_lib)
  if(MOG_WITH_CLI11)
    target_link_libraries(${PROJECT_NAME}_app PRIVATE CLI11::CLI11)
  endif()
  if(MOG_WITH_JSON)
    target_link_libraries(${PROJECT_NAME}_app PRIVATE nlohmann_json::nlohmann_json)
  endif()
  if(MOG_WITH_SPDLOG)
    target_link_libraries(${PROJECT_NAME}_app PRIVATE spdlog::spdlog)
  endif()
  cppboot_set_project_warnings(${PROJECT_NAME}_app)
endif()
