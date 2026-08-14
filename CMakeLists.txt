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
  DESCRIPTION "Lightweight cross-platform HTTP/S client library and CLI"
  LANGUAGES C CXX
)

# Apple native backend is Objective-C++ (NSURLSession); enable OBJCXX there.
if(APPLE)
  enable_language(OBJCXX)
endif()

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
# Absolute-path includes: module-name include() resolves against
# CMAKE_MODULE_PATH, and when mog is embedded (add_subdirectory /
# FetchContent) the PARENT project's cmake/ is searched first — a
# same-named parent module silently shadows ours (observed embedding mog
# into SDL3-static-extensions, where the parent's Dependencies.cmake won
# and mog's mbedTLS/miniz were never declared).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake")

# Preferred third-party libraries (FetchContent). On for top-level native apps;
# off when embedded and for the Android/iOS packages, whose public contract is
# the dependency-free core C++ API.
set(MOG_DEFAULT_WITH_OPTIONAL_DEPS ${MOG_IS_TOP_LEVEL})
if(ANDROID OR IOS)
  set(MOG_DEFAULT_WITH_OPTIONAL_DEPS OFF)
endif()
option(MOG_WITH_CLI11 "CLI argument parsing via CLI11" ${MOG_DEFAULT_WITH_OPTIONAL_DEPS})
option(MOG_WITH_JSON "JSON parsing via nlohmann/json" ${MOG_DEFAULT_WITH_OPTIONAL_DEPS})
option(MOG_WITH_SPDLOG "Console/file logging via spdlog" ${MOG_DEFAULT_WITH_OPTIONAL_DEPS})

set(MOG_DEFAULT_BUILD_APP ${MOG_IS_TOP_LEVEL})
set(MOG_DEFAULT_BUILD_TESTS ${MOG_IS_TOP_LEVEL})
set(MOG_DEFAULT_BUILD_BENCHMARKS ${MOG_IS_TOP_LEVEL})
set(MOG_DEFAULT_BUILD_C_SHARED ${MOG_IS_TOP_LEVEL})
if(EMSCRIPTEN)
  # Browsers consume the static C++ library. They cannot host mog's CLI/server,
  # and Emscripten side modules are not the portable C-FFI package shipped on
  # native platforms.
  set(MOG_DEFAULT_BUILD_APP OFF)
  set(MOG_DEFAULT_BUILD_BENCHMARKS OFF)
  set(MOG_DEFAULT_BUILD_C_SHARED OFF)
endif()
if(ANDROID)
  # Android consumers receive the C++ library as a Prefab AAR. Repository tests
  # are hosted by the Gradle test application and run on an emulator/device.
  set(MOG_DEFAULT_BUILD_APP OFF)
  set(MOG_DEFAULT_BUILD_TESTS OFF)
  set(MOG_DEFAULT_BUILD_BENCHMARKS OFF)
  set(MOG_DEFAULT_BUILD_C_SHARED OFF)
endif()
if(IOS)
  # iOS consumers receive the C++ library as a static XCFramework. Repository
  # package tests are hosted by a separate application and run in Simulator.
  set(MOG_DEFAULT_BUILD_APP OFF)
  set(MOG_DEFAULT_BUILD_TESTS OFF)
  set(MOG_DEFAULT_BUILD_BENCHMARKS OFF)
  set(MOG_DEFAULT_BUILD_C_SHARED OFF)
endif()

# When embedded, skip app/tests/benchmarks unless the consumer opts in.
option(MOG_BUILD_APP "Build the demo application executable" ${MOG_DEFAULT_BUILD_APP})
option(MOG_BUILD_TESTS "Build unit tests" ${MOG_DEFAULT_BUILD_TESTS})
option(MOG_BUILD_BENCHMARKS "Build benchmarks" ${MOG_DEFAULT_BUILD_BENCHMARKS})
# ASan + UBSan for project targets (intended for Linux GCC/Clang; see make sanitizer).
option(MOG_ENABLE_SANITIZERS "Enable Address+UBSan on project targets" OFF)
# The C API (include/mog/mog_c.h) is shipped as a shared library for C and FFI
# callers. See src/c_api. On by default for top-level builds.
option(MOG_BUILD_C_SHARED "Build the mog_c shared library (C API for FFI)" ${MOG_DEFAULT_BUILD_C_SHARED})

if(EMSCRIPTEN AND (MOG_BUILD_APP OR MOG_BUILD_C_SHARED OR MOG_BUILD_BENCHMARKS))
  message(FATAL_ERROR
    "Emscripten builds support the static mog C++ library and browser tests only; "
    "set MOG_BUILD_APP, MOG_BUILD_C_SHARED, and MOG_BUILD_BENCHMARKS to OFF"
  )
endif()

if(ANDROID AND (MOG_BUILD_APP OR MOG_BUILD_TESTS OR MOG_BUILD_C_SHARED OR
                MOG_BUILD_BENCHMARKS))
  message(FATAL_ERROR
    "Android builds provide the mog Prefab library; set MOG_BUILD_APP, "
    "MOG_BUILD_TESTS, MOG_BUILD_C_SHARED, and MOG_BUILD_BENCHMARKS to OFF. "
    "Use the android Gradle project to build and run device tests."
  )
endif()

if(ANDROID AND (MOG_WITH_CLI11 OR MOG_WITH_JSON OR MOG_WITH_SPDLOG))
  message(FATAL_ERROR
    "The Android Prefab AAR contains the dependency-free core API; set "
    "MOG_WITH_CLI11, MOG_WITH_JSON, and MOG_WITH_SPDLOG to OFF"
  )
endif()

if(IOS AND (MOG_BUILD_APP OR MOG_BUILD_TESTS OR MOG_BUILD_C_SHARED OR
            MOG_BUILD_BENCHMARKS))
  message(FATAL_ERROR
    "iOS builds provide the mog XCFramework library; set MOG_BUILD_APP, "
    "MOG_BUILD_TESTS, MOG_BUILD_C_SHARED, and MOG_BUILD_BENCHMARKS to OFF. "
    "Use the iOS package test application for Simulator tests."
  )
endif()

if(IOS AND (MOG_WITH_CLI11 OR MOG_WITH_JSON OR MOG_WITH_SPDLOG))
  message(FATAL_ERROR
    "The iOS XCFramework contains the dependency-free core API; set "
    "MOG_WITH_CLI11, MOG_WITH_JSON, and MOG_WITH_SPDLOG to OFF"
  )
endif()

# Building a shared object from the otherwise-static core requires
# position-independent code throughout, including the FetchContent dependencies
# (mbedTLS, miniz). Enable PIC globally before those targets are declared. macOS
# and Windows are PIC/relocatable by default; this matters for Linux ELF, where
# linking non-PIC static archives into a .so fails ("failed to set dynamic
# section sizes"). The static musl scratch build sets MOG_BUILD_C_SHARED=OFF, so
# it is unaffected.
if(MOG_BUILD_C_SHARED OR ANDROID)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

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

if(EMSCRIPTEN)
  # The public request API is synchronous. The web transport awaits window.fetch
  # and Asyncify yields the Wasm stack to the browser until it completes. Keep
  # this transitive so every final consumer link gets the required transform.
  target_link_options(${PROJECT_NAME}_lib INTERFACE "SHELL:-sASYNCIFY=1")
endif()

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
  # gtest_discover_tests runs each freshly built test executable to enumerate its
  # cases. On Windows CI the first run of a new .exe is scanned by Defender,
  # which can exceed the default 5s discovery timeout when several run at once.
  # Give discovery a generous default; individual calls pass DISCOVERY_TIMEOUT
  # ${MOG_GTEST_DISCOVERY_TIMEOUT}.
  set(MOG_GTEST_DISCOVERY_TIMEOUT 120)
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
