cmake_minimum_required(VERSION 3.20)
project(shooting_rays_cpu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

find_package(OpenMP)

add_executable(shooting_rays main.cpp)

target_link_libraries(shooting_rays PRIVATE
    common
    glad
    glfw
    imgui
        embree4
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(shooting_rays PRIVATE OpenMP::OpenMP_CXX)
endif()
