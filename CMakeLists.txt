cmake_minimum_required(VERSION 3.16)
project(ode LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add optimization flags for macOS with LLVM
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

# Collect source files
file(GLOB_RECURSE SOURCES src/*.cpp)

# Create executable
add_executable(raytracer ${SOURCES})
