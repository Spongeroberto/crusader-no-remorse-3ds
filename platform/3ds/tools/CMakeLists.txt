cmake_minimum_required(VERSION 3.21)

# Tools for the computer that builds a console version, built with its own
# compiler from the engine's sources (cia.cmake builds this project).
project(crusader-host-tools CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(repo "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
add_executable(shape_reach "${repo}/src/tools/shape_reach.cpp" "${repo}/src/core/flex.cpp"
               "${repo}/src/core/shape.cpp" "${repo}/src/core/shape_reach.cpp")
target_include_directories(shape_reach PRIVATE "${repo}/src")
