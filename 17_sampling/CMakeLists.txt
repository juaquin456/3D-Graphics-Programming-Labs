cmake_minimum_required(VERSION 3.20)
project(sampling_lab17)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3)
endif()

find_package(OpenMP REQUIRED)

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common_build)

add_executable(17_sampling
    main.cpp
    src/gui/GuiManager.cpp
    src/gui/GuiManager.h
    src/sampling/SamplingGenerators.cpp
    src/sampling/SamplingGenerators.h
    src/sampling/PointCloudBuilder.cpp
    src/sampling/PointCloudBuilder.h
)

target_include_directories(17_sampling PRIVATE
    src
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(17_sampling PRIVATE
    common
    glad
    glfw
    imgui
    OpenMP::OpenMP_CXX
)
