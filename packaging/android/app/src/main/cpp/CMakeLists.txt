cmake_minimum_required(VERSION 3.22.1)
project(kymengine_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_app_glue STATIC
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c"
)
target_include_directories(native_app_glue PUBLIC
    "${ANDROID_NDK}/sources/android/native_app_glue"
)

add_library(kymengine_android SHARED
    android_main.cpp
)

target_link_libraries(kymengine_android PRIVATE
    native_app_glue
    android
    log
    vulkan
)

