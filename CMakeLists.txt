cmake_minimum_required(VERSION 3.21)
project(BarcodeScannerZXing VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# zxing-cpp: только чтение штрихкодов, без примеров и writer'ов.
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

if(ANDROID)
    # JNI-слой камеры (android/) подключится на этапе 2.
    target_link_libraries(${PROJECT_NAME} PRIVATE log)
endif()
