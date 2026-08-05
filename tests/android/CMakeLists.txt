cmake_minimum_required(VERSION 3.22.1)
project(mog_android_test LANGUAGES CXX)

find_package(mog REQUIRED CONFIG)

add_library(mog_android_test SHARED android_test.cpp)
target_compile_features(mog_android_test PRIVATE cxx_std_20)
target_compile_options(mog_android_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Werror
)
target_link_libraries(mog_android_test PRIVATE mog::mog log)
