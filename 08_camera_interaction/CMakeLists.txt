cmake_minimum_required(VERSION 3.20)
project(camera_interaction)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

add_executable(camera_interaction main.cpp
        Camera.h)

target_link_libraries(camera_interaction PRIVATE
    common
    glad
    glfw
)
