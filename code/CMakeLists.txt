CMAKE_MINIMUM_REQUIRED(VERSION 3.12)
PROJECT(PA1 LANGUAGES CXX)

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release)
ENDIF()

ADD_SUBDIRECTORY(deps/vecmath)

SET(PA1_SOURCES
        src/image.cpp
        src/main.cpp
        src/mesh.cpp
        src/scene_parser.cpp
        src/path_guiding.cpp
        src/bvh.cpp)

# CUDA 可选编译
FIND_PACKAGE(CUDA QUIET)
IF(CUDA_FOUND)
    ENABLE_LANGUAGE(CUDA)
    SET(CMAKE_CUDA_STANDARD 14)
    SET(CMAKE_CUDA_ARCHITECTURES "52;61;70;75;86")
    LIST(APPEND PA1_SOURCES src/gpu_render.cu)
ENDIF()

SET(PA1_INCLUDES
        include/camera.hpp
        include/group.hpp
        include/hit.hpp
        include/image.hpp
        include/light.hpp
        include/material.hpp
        include/mesh.hpp
        include/object3d.hpp
        include/plane.hpp
        include/ray.hpp
        include/scene_parser.hpp
        include/sphere.hpp
        include/transform.hpp
        include/triangle.hpp
        include/path_guiding.hpp
        include/gpu_render.h
        )

SET(CMAKE_CXX_STANDARD 11)

FIND_PACKAGE(OpenMP)
IF(OpenMP_CXX_FOUND)
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
ENDIF()

ADD_EXECUTABLE(${PROJECT_NAME} ${PA1_SOURCES} ${PA1_INCLUDES})
TARGET_LINK_LIBRARIES(${PROJECT_NAME} vecmath)
IF(OpenMP_CXX_FOUND)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} OpenMP::OpenMP_CXX)
ENDIF()
IF(CUDA_FOUND)
    TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PRIVATE USE_CUDA)
    TARGET_LINK_LIBRARIES(${PROJECT_NAME} ${CUDA_LIBRARIES})
    TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE ${CUDA_INCLUDE_DIRS})
ENDIF()
TARGET_INCLUDE_DIRECTORIES(${PROJECT_NAME} PRIVATE include)
