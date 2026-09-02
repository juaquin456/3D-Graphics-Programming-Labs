cmake_minimum_required(VERSION 3.20)
project(geodesic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

add_executable(geodesic main.cpp)

target_link_libraries(geodesic PRIVATE
    common
    glad
    glfw
)
