# HTTP client library component (embedded backend default).
target_sources(${PROJECT_NAME}_lib
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/error.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/options.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/response.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/url.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/detail/tls.cpp
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

if(WIN32)
  target_link_libraries(${PROJECT_NAME}_lib PRIVATE $<BUILD_INTERFACE:ws2_32>)
endif()
