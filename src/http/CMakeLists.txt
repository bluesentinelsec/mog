# HTTP client library component (embedded backend default).
target_sources(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/error.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/options.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/response.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/util.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/url.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/tls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/prepare.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/content_encoding.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/ca_store.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/mozilla_ca_bundle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/connection_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/cookie_jar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/digest_auth.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/multipart.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/embedded_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/registry.cpp
)

# Platform-native backend: NSURLSession on macOS (Objective-C++), stub elsewhere.
if(APPLE)
  target_sources(${PROJECT_NAME}_lib
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/detail/native_backend_apple.mm)
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/detail/native_backend_apple.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  target_link_libraries(${PROJECT_NAME}_lib PRIVATE "-framework Foundation")
else()
  target_sources(${PROJECT_NAME}_lib
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/detail/native_backend_stub.cpp)
endif()

# Private headers under src/http (detail/*).
target_include_directories(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# mbedTLS is required for the embedded HTTPS backend.
# Wrap in BUILD_INTERFACE so install(EXPORT) does not require mbedTLS targets
# (they are statically linked into mog_lib for consumers of the archive).
target_link_libraries(${PROJECT_NAME}_lib
  PRIVATE
    $<BUILD_INTERFACE:mbedtls>
    $<BUILD_INTERFACE:mbedx509>
    $<BUILD_INTERFACE:mbedcrypto>
    $<BUILD_INTERFACE:miniz>
)

# nlohmann/json — cppboot preferred JSON library. Header-only; enables mog/json.hpp
# and MOG_HAS_JSON for consumers of this build tree.
if(MOG_WITH_JSON)
  target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
  )
  target_compile_definitions(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:MOG_HAS_JSON=1>
  )
endif()

# spdlog — cppboot preferred logging; static-link friendly. Enables mog/log.hpp
# injection API and detailed request tracing in the library + CLI.
if(MOG_WITH_SPDLOG)
  target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:spdlog::spdlog>
  )
  target_compile_definitions(${PROJECT_NAME}_lib
    PUBLIC
      $<BUILD_INTERFACE:MOG_HAS_SPDLOG=1>
  )
endif()

if(WIN32)
  target_link_libraries(${PROJECT_NAME}_lib PRIVATE $<BUILD_INTERFACE:ws2_32>)
endif()
