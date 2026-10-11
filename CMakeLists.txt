cmake_minimum_required(VERSION 3.21)
project(deephex VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 COMPONENTS Widgets REQUIRED)

set(VIBRANCY_SOURCE src/native_vibrancy.cpp)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(VIBRANCY_SOURCE src/native_vibrancy.mm)
    set_source_files_properties(src/native_vibrancy.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
endif()

add_executable(deephex MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/memoryview.cpp src/memoryview.h
    src/heatmap.cpp src/heatmap.h
    src/standard_map.cpp src/standard_map.h
    src/elf_map.cpp src/elf_map.h
    src/macho_map.cpp src/macho_map.h
    resources.qrc
    src/native_vibrancy.h ${VIBRANCY_SOURCE}
)
if(APPLE)
    target_sources(deephex PRIVATE img/favicon.icns)
    set_source_files_properties(img/favicon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_link_libraries(deephex PRIVATE "${COCOA_FRAMEWORK}")
endif()
target_link_libraries(deephex PRIVATE Qt6::Widgets)
target_compile_options(deephex PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Wpedantic>
)
set_target_properties(deephex PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)
install(TARGETS deephex BUNDLE DESTINATION . RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 COMPONENTS Test REQUIRED)
    add_executable(deephex_tests
        tests/editor_tests.cpp
        src/mainwindow.cpp src/mainwindow.h
        src/memoryview.cpp src/memoryview.h
        src/heatmap.cpp src/heatmap.h
        src/standard_map.cpp src/standard_map.h
        src/elf_map.cpp src/elf_map.h
        src/macho_map.cpp src/macho_map.h
        src/native_vibrancy.h ${VIBRANCY_SOURCE}
    )
    target_include_directories(deephex_tests PRIVATE src)
    target_link_libraries(deephex_tests PRIVATE Qt6::Widgets Qt6::Test)
    if(APPLE)
        target_link_libraries(deephex_tests PRIVATE "${COCOA_FRAMEWORK}")
    endif()
    add_test(NAME editor_behavior COMMAND deephex_tests -platform offscreen)
endif()
