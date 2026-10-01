cmake_minimum_required(VERSION 4.3)
project(Emulator)

set(CMAKE_CXX_STANDARD 20)

add_executable(Emulator main.cpp
        parser.cpp
        emulator.cpp
        registers.cpp
        stack.cpp
        util.cpp)
