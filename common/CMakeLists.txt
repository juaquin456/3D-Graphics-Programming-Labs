cmake_minimum_required(VERSION 3.20)
project(common)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Create interface targets for external dependencies if not already defined
if(NOT TARGET linalg)
    add_library(linalg INTERFACE)
    target_include_directories(linalg INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../external/linalg")
endif()

if(NOT TARGET happly)
    add_library(happly INTERFACE)
    target_include_directories(happly INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../external/happly")
endif()

add_library(common STATIC
    src/Mesh.cpp
    src/HalfEdgeMesh.cpp
)

target_include_directories(common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(common PUBLIC
    linalg
    happly
)
