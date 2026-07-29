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
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/embedded_backend.cpp
)

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
