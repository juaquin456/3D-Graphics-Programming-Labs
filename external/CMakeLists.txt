cmake_minimum_required(VERSION 3.20)
include(FetchContent)
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863 # Version 1.0.1
)
FetchContent_MakeAvailable(glm)

add_library(glad STATIC glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)

add_library(happly INTERFACE)
target_include_directories(happly INTERFACE happly/)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE stb/)

FetchContent_Declare(
        tinyobjloader_src
        GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
        GIT_TAG        v2.0.0rc13
)
FetchContent_GetProperties(tinyobjloader_src)
if(NOT tinyobjloader_src_POPULATED)
    FetchContent_Populate(tinyobjloader_src)
endif()
add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE "${tinyobjloader_src_SOURCE_DIR}")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a")
    add_library(glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
    )
endif()
