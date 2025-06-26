cmake_minimum_required(VERSION 3.16)
project(deephex VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Enable compile_commands.json generation
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable AddressSanitizer to debug
# set(SANITIZER_FLAGS "-fsanitize=address -fno-omit-frame-pointer")

add_executable(deephex
    src/heatmap.cpp
    src/heatmap.h
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/memoryview.cpp
    src/memoryview.h
    src/standard_map.cpp
    src/standard_map.h
    src/elf_map.cpp
    src/elf_map.h
    src/macho_map.cpp
    src/macho_map.h
)

target_link_libraries(deephex PRIVATE Qt6::Widgets)