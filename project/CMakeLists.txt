cmake_minimum_required(VERSION 3.20)
project(project)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

add_executable(project
    main.cpp
    src/gui/GuiManager.cpp
    src/gui/GuiManager.h
)

target_include_directories(project PRIVATE src)

target_link_libraries(project PRIVATE
    common
    glad
    glfw
    imgui
)
