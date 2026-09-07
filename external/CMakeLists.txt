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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a")
    add_library(glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
    )
endif()
