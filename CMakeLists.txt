cmake_minimum_required(VERSION 3.21)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(deephex VERSION 2.0.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT APPLE)
    message(FATAL_ERROR "DeepHex requires macOS and AppKit.")
endif()
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(APPKIT_FRAMEWORK AppKit REQUIRED)

add_library(deephex_maps STATIC
    src/standard_map.cpp src/standard_map.h
    src/elf_map.cpp src/elf_map.h
    src/macho_map.cpp src/macho_map.h
)
target_include_directories(deephex_maps PUBLIC src)

add_library(deephex_ui STATIC
    src/mainwindow.mm src/mainwindow.h
    src/memoryview.mm src/memoryview.h
    src/heatmap.mm src/heatmap.h
)
target_compile_options(deephex_ui PRIVATE -fobjc-arc -Wall -Wextra)
target_link_libraries(deephex_ui PUBLIC deephex_maps "${APPKIT_FRAMEWORK}")

add_executable(deephex MACOSX_BUNDLE
    src/main.mm
    img/favicon.icns
)
set_source_files_properties(img/favicon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_compile_options(deephex PRIVATE -fobjc-arc -Wall -Wextra)
target_link_libraries(deephex PRIVATE deephex_ui)
set_target_properties(deephex PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
)
install(TARGETS deephex BUNDLE DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    add_executable(deephex_map_tests tests/map_tests.cpp)
    target_link_libraries(deephex_map_tests PRIVATE deephex_maps)
    add_test(NAME executable_maps COMMAND deephex_map_tests)
    add_executable(deephex_editor_tests tests/editor_tests.mm)
    target_compile_options(deephex_editor_tests PRIVATE -fobjc-arc)
    target_link_libraries(deephex_editor_tests PRIVATE deephex_ui)
    add_test(NAME editor_behavior COMMAND deephex_editor_tests)
    add_executable(deephex_window_tests tests/window_tests.mm)
    target_compile_options(deephex_window_tests PRIVATE -fobjc-arc)
    target_link_libraries(deephex_window_tests PRIVATE deephex_ui)
    add_test(NAME document_behavior COMMAND deephex_window_tests)
    set_tests_properties(editor_behavior document_behavior PROPERTIES RUN_SERIAL TRUE)
endif()
