cmake_minimum_required(VERSION 3.20)
project(path_tracer_bsdf LANGUAGES C CXX CUDA)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -march=native -ffast-math -flto)
elseif(MSVC)
    add_compile_options(/O2 /arch:AVX2 /fp:fast /LTCG)
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/../external ${CMAKE_BINARY_DIR}/external_build)
add_subdirectory(${CMAKE_SOURCE_DIR}/../common   ${CMAKE_BINARY_DIR}/common_build)
set(GLM_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../external")
find_package(OpenMP REQUIRED)
find_package(CUDAToolkit REQUIRED)
find_package(embree 4 QUIET)

if(NOT embree_FOUND)
    find_path(EMBREE_INCLUDE embree4/rtcore.h PATHS /usr/include)
    find_library(EMBREE_LIB NAMES embree4 PATHS /usr/lib /usr/lib/x86_64-linux-gnu)
    if(EMBREE_INCLUDE AND EMBREE_LIB)
        message(STATUS "Embree4 found (manual): include=${EMBREE_INCLUDE}, lib=${EMBREE_LIB}")
        set(EMBREE_FOUND TRUE)
    else()
        message(FATAL_ERROR "Intel Embree4 not found! Install: sudo apt install libembree-dev")
    endif()
else()
    message(STATUS "Embree4 found via find_package")
    set(EMBREE_INCLUDE "")
    set(EMBREE_LIB embree4)
endif()

set(OPTIX_SDK_DIR "${CMAKE_SOURCE_DIR}/../external/optix" CACHE PATH "OptiX SDK root")
set(OPTIX_INCLUDE "${OPTIX_SDK_DIR}/include")
if(NOT EXISTS "${OPTIX_INCLUDE}/optix.h")
    message(FATAL_ERROR "optix.h not found at ${OPTIX_INCLUDE}. Set OPTIX_SDK_DIR.")
endif()
message(STATUS "OptiX SDK include: ${OPTIX_INCLUDE}")

set(CUDA_ARCH "120" CACHE STRING "CUDA compute architecture (e.g. 75 for RTX 20xx, 86 for RTX 30xx, 89 for RTX 40xx, 120 for RTX 50xx)")
message(STATUS "Compiling OptiX PTX for sm_${CUDA_ARCH}")

set(PTX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/optix_kernels.ptx")

add_custom_command(
        OUTPUT  ${PTX_OUTPUT}
        COMMAND ${CMAKE_CUDA_COMPILER}
        -ptx
        -O3
        --use_fast_math
        -arch=compute_${CUDA_ARCH}
        -I${OPTIX_INCLUDE}
        -I${CMAKE_CURRENT_SOURCE_DIR}
        -I${CMAKE_SOURCE_DIR}/../common
        -I${CMAKE_SOURCE_DIR}/../external
        -I${CMAKE_BINARY_DIR}/_deps/glm-src
        -DGLM_FORCE_CUDA
        -DGLM_FORCE_PURE
        -DGLM_CUDA_VERSION=11000
        -DGLM_FORCE_CTOR_INIT
        --std=c++17
        --expt-relaxed-constexpr
        -o ${PTX_OUTPUT}
        ${CMAKE_CURRENT_SOURCE_DIR}/optix_kernels.cu
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/optix_kernels.cu
        ${CMAKE_CURRENT_SOURCE_DIR}/LaunchParams.h
        ${CMAKE_CURRENT_SOURCE_DIR}/SceneTypes.h
        COMMENT "Compiling OptiX device kernels to PTX (Release/FastMath)"
)
add_custom_target(optix_ptx ALL DEPENDS ${PTX_OUTPUT})

add_custom_command(
        TARGET optix_ptx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${PTX_OUTPUT} ${CMAKE_BINARY_DIR}/optix_kernels.ptx
        COMMENT "Copying PTX to binary directory"
)

add_executable(path_tracer_bsdf
        main.cpp
        EmbreePathTracer.cpp
        OptixPathTracer.cpp
)

add_dependencies(path_tracer_bsdf optix_ptx)

target_include_directories(path_tracer_bsdf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OPTIX_INCLUDE}
        ${EMBREE_INCLUDE}
)

target_link_libraries(path_tracer_bsdf PRIVATE
        common
        glad
        glfw
        imgui
        glm::glm
        OpenMP::OpenMP_CXX
        ${EMBREE_LIB}
        CUDA::cudart
        ${CMAKE_DL_LIBS}
        dl
)