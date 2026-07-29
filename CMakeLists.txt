cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Single source of truth for the package version: the VERSION file at repo root.
# Edit that file only; CMake, --version, tests, and release checks all consume it.
# ---------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MOG_VERSION_RAW
  LIMIT_COUNT 1
)
string(STRIP "${MOG_VERSION_RAW}" MOG_VERSION)
# Allow optional leading 'v' and ignore anything after '#' on the line.
string(REGEX REPLACE "^[vV]" "" MOG_VERSION "${MOG_VERSION}")
string(REGEX REPLACE "[ \t]*#.*" "" MOG_VERSION "${MOG_VERSION}")
string(STRIP "${MOG_VERSION}" MOG_VERSION)
if(NOT MOG_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([.-].*)?$")
  message(FATAL_ERROR
    "VERSION file must contain a semantic version like 0.1.0 "
    "(got '${MOG_VERSION}')"
  )
endif()

project(mog
  VERSION ${MOG_VERSION}
  DESCRIPTION "C++ project bootstrapped by cppboot"
  LANGUAGES CXX
)

# True when this project is the top-level CMake project (not add_subdirectory /
# FetchContent). Downstream consumers should not inherit app/tests/bench defaults.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MOG_IS_TOP_LEVEL ON)
else()
  set(MOG_IS_TOP_LEVEL OFF)
endif()

# Exposed to version templates (configure_file @ONLY).
set(PROJECT_NAMESPACE "mog")
set(PROJECT_VERSION_STRING "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Top-level-only settings so embedding via add_subdirectory/FetchContent stays clean.
if(MOG_IS_TOP_LEVEL)
  # Export a compilation database for clangd and other LSP tools.
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

  # Put binaries under build/<config>/bin (and libs under lib/) so an executable
  # named like a component directory (e.g. project "calc" + src/calc/) never
  # collides with a source/build subdirectory path.
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

  # Default to Debug when the user does not pass CMAKE_BUILD_TYPE (single-config).
  if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# Preferred third-party libraries (FetchContent). On for top-level apps; off when
# embedded so consumers do not pull CLI/JSON/logging unless they opt in.
option(MOG_WITH_CLI11 "CLI argument parsing via CLI11" ${MOG_IS_TOP_LEVEL})
option(MOG_WITH_JSON "JSON parsing via nlohmann/json" ${MOG_IS_TOP_LEVEL})
option(MOG_WITH_SPDLOG "Console/file logging via spdlog" ${MOG_IS_TOP_LEVEL})

# When embedded, skip app/tests/benchmarks unless the consumer opts in.
option(MOG_BUILD_APP "Build the demo application executable" ${MOG_IS_TOP_LEVEL})
option(MOG_BUILD_TESTS "Build unit tests" ${MOG_IS_TOP_LEVEL})
option(MOG_BUILD_BENCHMARKS "Build benchmarks" ${MOG_IS_TOP_LEVEL})
# ASan + UBSan for project targets (intended for Linux GCC/Clang; see make sanitizer).
option(MOG_ENABLE_SANITIZERS "Enable Address+UBSan on project targets" OFF)

include(Dependencies)

# Apply sanitizer flags only to targets created after this point (not FetchContent deps).
if(MOG_ENABLE_SANITIZERS)
  cppboot_enable_sanitizers()
endif()

# Generate the version API from cmake/version.{hpp,cpp}.in (values from VERSION).
set(MOG_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(MOG_GENERATED_INCLUDE_DIR "${MOG_GENERATED_DIR}/include")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
  "${MOG_GENERATED_INCLUDE_DIR}/mog/version.hpp"
  @ONLY
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cpp.in"
  "${MOG_GENERATED_DIR}/version.cpp"
  @ONLY
)

add_library(${PROJECT_NAME}_lib STATIC)
# Namespaced aliases for consumers (add_subdirectory / FetchContent / find_package).
add_library(${PROJECT_NAME}::lib ALIAS ${PROJECT_NAME}_lib)
add_library(${PROJECT_NAME}::mog ALIAS ${PROJECT_NAME}_lib)

set_target_properties(${PROJECT_NAME}_lib PROPERTIES
  OUTPUT_NAME mog
  EXPORT_NAME lib
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(${PROJECT_NAME}_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MOG_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

cppboot_set_project_warnings(${PROJECT_NAME}_lib)

# Preferred third-party deps are linked on the *app* (and optionally tests), not
# the library. Static libraries propagate private deps into install(EXPORT);
# keeping the library free of FetchContent targets makes find_package clean.
# Add target_link_libraries(... PUBLIC/PRIVATE ...) in component CMakeLists when
# library code needs these deps.

add_subdirectory(src)

if(MOG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MOG_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# Keep a source-root compile_commands.json for clangd / VS Code IntelliSense.
# Only when this project is top-level (do not rewrite a parent project's link).
if(MOG_IS_TOP_LEVEL AND CMAKE_EXPORT_COMPILE_COMMANDS)
  if(WIN32)
    add_custom_target(cppboot_compile_commands ALL
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
      COMMENT "Copying compile_commands.json to source root for clangd"
      VERBATIM
    )
  else()
    add_custom_target(cppboot_compile_commands ALL
      COMMAND ${CMAKE_COMMAND} -E rm -f
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
      COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
      COMMENT "Linking compile_commands.json to source root for clangd"
      VERBATIM
    )
  endif()
endif()

include(GNUInstallDirs)
install(DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)
# Generated version API header (from VERSION + cmake/version.hpp.in).
install(DIRECTORY ${MOG_GENERATED_INCLUDE_DIR}/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)
install(TARGETS ${PROJECT_NAME}_lib
  EXPORT ${PROJECT_NAME}Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# CMake package config so consumers can find_package(mog).
install(EXPORT ${PROJECT_NAME}Targets
  FILE ${PROJECT_NAME}Targets.cmake
  NAMESPACE ${PROJECT_NAME}::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mogConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/mogConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mogConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/mogConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/mogConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

