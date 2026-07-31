# C binding layer: an extern "C" API over the C++ library, shipped as a shared
# library so C programs and FFI runtimes (Python ctypes, LuaJIT, Ruby Fiddle,
# etc.) can call mog. The C++ core (and mbedTLS / miniz / OS frameworks) is
# linked in privately, so the shared library is self-contained and exports only
# the C API surface declared in include/mog/mog_c.h.
option(MOG_BUILD_C_SHARED "Build the mog_c shared library (C API for FFI)" ${MOG_IS_TOP_LEVEL})

if(MOG_BUILD_C_SHARED)
  add_library(${PROJECT_NAME}_c SHARED mog_c.cpp)
  add_library(${PROJECT_NAME}::c ALIAS ${PROJECT_NAME}_c)

  set_target_properties(${PROJECT_NAME}_c PROPERTIES
    OUTPUT_NAME mog_c
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
  )

  # Mark the C API symbols for export (dllexport / default visibility).
  target_compile_definitions(${PROJECT_NAME}_c PRIVATE MOG_C_BUILD_SHARED)

  target_include_directories(${PROJECT_NAME}_c
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${MOG_GENERATED_INCLUDE_DIR}>
      $<INSTALL_INTERFACE:include>
  )

  target_link_libraries(${PROJECT_NAME}_c PRIVATE ${PROJECT_NAME}_lib)

  cppboot_set_project_warnings(${PROJECT_NAME}_c)

  install(TARGETS ${PROJECT_NAME}_c
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
