# DeepHex

A simple Qt-based memory viewer application that displays data in a hex format.

## Requirements

- CMake 3.16 or higher
- Qt 6.x
- C++17 compatible compiler

## Building

```bash
mkdir build
cd build 
cmake .. -DQt6_DIR=/Users/df0/Qt/6.9.1/macos/lib/cmake/Qt6
make
```

## Running

After building, you can run the application:

```bash
./memview
```

## Features

- Displays memory data in a hex format
- Shows 16 bytes per line
- Includes memory addresses
- Clean and readable interface

## Usage

The application currently displays a sample file view.