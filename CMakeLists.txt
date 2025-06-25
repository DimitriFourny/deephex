cmake_minimum_required(VERSION 3.16)
project(memview VERSION 1.0 LANGUAGES CXX)

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

add_executable(memview
    heatmap.cpp
    heatmap.h
    main.cpp
    mainwindow.cpp
    mainwindow.h
    memoryview.cpp
    memoryview.h
)

target_link_libraries(memview PRIVATE Qt6::Widgets)