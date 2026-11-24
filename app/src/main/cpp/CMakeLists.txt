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

set(UNRAR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unrar)
if(EXISTS ${UNRAR_DIR}/dll.cpp)
    set(UNRAR_NAMES rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode system crypt crc
        rawread encname resource match timefn rdwrfn consio options errhnd rarvm secpassword rijndael getbits sha1 sha256
        blake2s hash extinfo extract volume list find unpack headers threadpool rs16 cmddata ui largepage filestr scantree
        dll qopen)
    set(UNRAR_SOURCES "")
    foreach(n ${UNRAR_NAMES})
        list(APPEND UNRAR_SOURCES ${UNRAR_DIR}/${n}.cpp)
    endforeach()
    add_library(unrar STATIC ${UNRAR_SOURCES})
    target_compile_definitions(unrar PRIVATE RARDLL SILENT RAR_SMP _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
    target_compile_options(unrar PRIVATE -O2 -w -fexceptions -frtti)
    target_include_directories(windroid PRIVATE ${UNRAR_DIR})
    target_compile_definitions(windroid PRIVATE HAVE_UNRAR=1)
    target_link_libraries(windroid unrar)
endif()
