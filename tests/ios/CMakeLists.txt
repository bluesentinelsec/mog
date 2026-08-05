cmake_minimum_required(VERSION 3.28)

project(mog_ios_package_test LANGUAGES CXX OBJCXX)

if(NOT IOS)
  message(FATAL_ERROR "The mog iOS package test must be configured with CMAKE_SYSTEM_NAME=iOS")
endif()

set(MOG_XCFRAMEWORK "" CACHE PATH "Path to the packaged mog.xcframework")
set(MOG_EXPECTED_VERSION "" CACHE STRING "Version expected from the packaged library")

if(NOT IS_DIRECTORY "${MOG_XCFRAMEWORK}")
  message(FATAL_ERROR "Set MOG_XCFRAMEWORK to the packaged mog.xcframework directory")
endif()
if(MOG_EXPECTED_VERSION STREQUAL "")
  message(FATAL_ERROR "Set MOG_EXPECTED_VERSION to the root VERSION value")
endif()

add_executable(mog_ios_test MACOSX_BUNDLE test_main.mm)
target_compile_features(mog_ios_test PRIVATE cxx_std_20)
target_compile_options(mog_ios_test PRIVATE -fobjc-arc)
target_compile_definitions(mog_ios_test PRIVATE
  MOG_EXPECTED_VERSION="${MOG_EXPECTED_VERSION}"
)
target_link_libraries(mog_ios_test PRIVATE
  "${MOG_XCFRAMEWORK}"
  "-framework Foundation"
  "-framework UIKit"
)

set_target_properties(mog_ios_test PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.bluesentinelsec.mog.test"
  MACOSX_BUNDLE_BUNDLE_NAME "mog iOS Tests"
  MACOSX_BUNDLE_BUNDLE_VERSION "1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${MOG_EXPECTED_VERSION}"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.bluesentinelsec.mog.test"
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
  XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
)
