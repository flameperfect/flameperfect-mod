cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
project(Flameperfect VERSION 1.2.0)
add_library(${PROJECT_NAME} SHARED src/main.cpp)
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})

# libwebp (decoder + animation demuxer) for pack startup intros
CPMAddPackage(
    NAME libwebp
    GITHUB_REPOSITORY webmproject/libwebp
    GIT_TAG v1.4.0
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "WEBP_BUILD_ANIM_UTILS OFF" "WEBP_BUILD_CWEBP OFF" "WEBP_BUILD_DWEBP OFF"
        "WEBP_BUILD_GIF2WEBP OFF" "WEBP_BUILD_IMG2WEBP OFF" "WEBP_BUILD_VWEBP OFF"
        "WEBP_BUILD_WEBPINFO OFF" "WEBP_BUILD_LIBWEBPMUX OFF" "WEBP_BUILD_WEBPMUX OFF"
        "WEBP_BUILD_EXTRAS OFF"
)
target_link_libraries(${PROJECT_NAME} webpdemux)
