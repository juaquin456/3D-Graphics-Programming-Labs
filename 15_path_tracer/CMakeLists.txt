cmake_minimum_required(VERSION 3.20)
project(path_tracer LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

find_package(OpenMP)

set(SOURCES
    main.cpp
    CudaRayTracer.cu
)

add_executable(path_tracer ${SOURCES})

target_link_libraries(path_tracer PRIVATE
    common
    glad
    glfw
    imgui
)

target_include_directories(path_tracer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(OpenMP_CXX_FOUND)
    target_link_libraries(path_tracer PRIVATE OpenMP::OpenMP_CXX)
endif()
