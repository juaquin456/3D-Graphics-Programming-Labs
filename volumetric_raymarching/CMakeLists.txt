cmake_minimum_required(VERSION 3.20)
project(volumetric_raymarching LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# High-performance compiler optimization flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -ffast-math)
elseif(MSVC)
    add_compile_options(/O2 /arch:AVX2 /fp:fast)
endif()

# Ensure local offline-friendly dependencies (never fail on missing network)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../external/glm")
    set(FETCHCONTENT_SOURCE_DIR_GLM "${CMAKE_CURRENT_SOURCE_DIR}/../external/glm" CACHE PATH "Local GLM" FORCE)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui")
    set(FETCHCONTENT_SOURCE_DIR_IMGUI "${CMAKE_CURRENT_SOURCE_DIR}/../external/imgui" CACHE PATH "Local ImGui" FORCE)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)

file(GLOB_RECURSE SRC_FILES 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(volumetric_raymarching
    main.cpp
    ${SRC_FILES}
)

target_include_directories(volumetric_raymarching PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/../external"
    "${CMAKE_SOURCE_DIR}/../external/glad/include"
    "${CMAKE_SOURCE_DIR}/../external/glfw/include"
)

target_link_libraries(volumetric_raymarching PRIVATE
    glad
    glfw
    imgui
    glm::glm
    ${CMAKE_DL_LIBS}
    dl
    pthread
)

# Automatically synchronize shaders directory to the runtime directory
add_custom_command(TARGET volumetric_raymarching POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:volumetric_raymarching>/shaders"
    COMMENT "Copying shaders to executable output directory"
)
