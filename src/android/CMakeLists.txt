# Android Prefab library. Gradle publishes this target as the `mog` module in
# the release AAR. Whole-archive linking keeps every public C++ API symbol while
# folding the embedded HTTP/TLS implementation and its C dependencies into one
# shared object.
add_library(mog SHARED android_library.cpp)

target_compile_features(mog PUBLIC cxx_std_20)
target_include_directories(mog
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${MOG_GENERATED_INCLUDE_DIR}>
)

add_dependencies(mog ${PROJECT_NAME}_lib)
target_link_options(mog
  PRIVATE
    "LINKER:--whole-archive,$<TARGET_FILE:${PROJECT_NAME}_lib>,--no-whole-archive"
)
target_link_libraries(mog
  PRIVATE
    mbedtls
    mbedx509
    mbedcrypto
    miniz
    ${CMAKE_DL_LIBS}
)

set_target_properties(mog PROPERTIES
  OUTPUT_NAME mog
)

cppboot_set_project_warnings(mog)
