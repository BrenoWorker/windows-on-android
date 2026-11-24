cmake_minimum_required(VERSION 3.22.1)
project(windroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB_RECURSE WINDROID_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shell/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/apps/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.cpp)

add_library(windroid SHARED ${WINDROID_SOURCES})
target_include_directories(windroid PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(windroid PRIVATE -Wall -Wno-unused-parameter -O2)
target_link_options(windroid PRIVATE -u ANativeActivity_onCreate)
target_link_libraries(windroid native_app_glue android EGL GLESv2 log z)
