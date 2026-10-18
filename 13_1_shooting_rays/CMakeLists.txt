cmake_minimum_required(VERSION 3.20)
project(shooting_rays LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    set(HAVE_CUDA ON)
    add_compile_definitions(HAVE_CUDA=1)
    message(STATUS "CUDA Support: ENABLED (${CMAKE_CUDA_COMPILER})")
else()
    set(HAVE_CUDA OFF)
    message(STATUS "CUDA Support: DISABLED (CUDA compiler not found)")
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

find_package(OpenMP)

set(SOURCES main.cpp)
if(HAVE_CUDA)
    list(APPEND SOURCES CudaRayTracer.cu)
endif()

add_executable(shooting_rays ${SOURCES})

target_link_libraries(shooting_rays PRIVATE
    common
    glad
    glfw
    imgui
)

if(HAVE_CUDA)
    target_include_directories(shooting_rays PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(shooting_rays PRIVATE OpenMP::OpenMP_CXX)
endif()
