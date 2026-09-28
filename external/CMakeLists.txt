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


add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader INTERFACE tinyobjloader/)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a")
    add_library(glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/glfw/lib/libglfw3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/glfw/include"
    )
endif()


FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.90.8
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_options(imgui PUBLIC -DGLFW_INCLUDE_NONE)

target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/glfw/include
        ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

if(TARGET glfw)
    target_link_libraries(imgui PUBLIC glfw glad)
else()
    target_link_libraries(imgui PUBLIC glad)
endif()