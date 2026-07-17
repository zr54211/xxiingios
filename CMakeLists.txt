cmake_minimum_required(VERSION 3.21)
project(BarcodeScannerZXing VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# zxing-cpp: только чтение штрихкодов, без примеров и writer'ов.
# Статически внутрь нашего бинарника — в zip ВК едет один self-contained .so.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/zxing-cpp EXCLUDE_FROM_ALL)

set(CORE_SOURCES
    core/exports.cpp
    core/BarcodeScannerAddIn.cpp
    core/BarcodeScannerAddIn.h
    core/Decoder.cpp
    core/Decoder.h
    core/StringUtils.cpp
    core/StringUtils.h)

if(IOS)
    # iOS-компоненты линкуются в приложение статически сборщиком 1С.
    add_library(${PROJECT_NAME} STATIC ${CORE_SOURCES})
else()
    add_library(${PROJECT_NAME} SHARED ${CORE_SOURCES})
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    vendor/1c-native-api/include
    core)

target_link_libraries(${PROJECT_NAME} PRIVATE ZXing::ZXing)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

if(MSVC)
    # Исходники в UTF-8 (кириллица в литералах контракта).
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

if(WIN32)
    # Windows-сборка нужна только для быстрой отладки контракта в тонком клиенте.
    target_sources(${PROJECT_NAME} PRIVATE core/exports.def)
endif()

if(IOS)
    enable_language(OBJCXX)
    target_sources(${PROJECT_NAME} PRIVATE
        ios/IosScanner.mm
        ios/IosScanner.h)
    target_include_directories(${PROJECT_NAME} PRIVATE ios)
    # UIKit статической библиотеке не линкуется — приедет из приложения платформы.
    set_source_files_properties(ios/IosScanner.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

if(ANDROID)
    target_sources(${PROJECT_NAME} PRIVATE
        android/AndroidScanner.cpp
        android/AndroidScanner.h
        android/CameraScanner.cpp
        android/CameraScanner.h)
    target_include_directories(${PROJECT_NAME} PRIVATE android)
    # camera2ndk/mediandk загружаются через dlopen (API 24+ при сборке под android-21);
    # без weak-режима NDK запрещает даже decltype на функциях API 24.
    set_source_files_properties(android/CameraScanner.cpp PROPERTIES
        COMPILE_DEFINITIONS __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
    target_link_libraries(${PROJECT_NAME} PRIVATE log android)
endif()
