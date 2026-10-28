cmake_minimum_required(VERSION 3.20)
project(shooting_rays_cuda LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

add_executable(shooting_rays_cuda
    main.cpp
    CudaRayTracer.cu
)

target_link_libraries(shooting_rays_cuda PRIVATE
    common
    glad
    glfw
    imgui
)

target_include_directories(shooting_rays_cuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
