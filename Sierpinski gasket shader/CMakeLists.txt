cmake_minimum_required(VERSION 3.20)
project(learn_opengl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/../common/include)

add_executable(learn_opengl main.cpp ${CMAKE_SOURCE_DIR}/../common/src/glad.c)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/../common/lib/libglfw3.a
)
