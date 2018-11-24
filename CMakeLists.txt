cmake_minimum_required(VERSION 3.0)
set(CMAKE_CXX_STANDARD 14)

set(PROJECT_ROOT_PATH ${CMAKE_SOURCE_DIR})
set(BINARY_DIR ${PROJECT_ROOT_PATH}/release/)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BINARY_DIR})

# Set search paths
set(DEPS_INCLUDE_PATH "" CACHE PATH "The path to depended includes")
set(DEPS_LIB_PATH "" CACHE PATH "The path to depended libraries")
set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${DEPS_INCLUDE_PATH} ${DEPS_LIB_PATH})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_ROOT_PATH}/cmake)

# Project
project(vxrt)

add_definitions(-DSDL_MAIN_HANDLED)
aux_source_directory(./src/ SOURCE)

# Link OpenGL & GLEW
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
set(DEPS_INCLUDE ${DEPS_INCLUDE} ${GLEW_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR})
set(DEPS_LIB ${DEPS_LIB} ${GLEW_LIBRARIES} ${OPENGL_gl_LIBRARY})

# Link SDL2
find_package(SDL2 REQUIRED)
set(DEPS_INCLUDE ${DEPS_INCLUDE} ${SDL2_INCLUDE_DIR})
set(DEPS_LIB ${DEPS_LIB} ${SDL2_LIBRARY})

add_executable(vxrt ${SOURCE})
target_include_directories(vxrt PUBLIC ${DEPS_INCLUDE})
target_link_libraries(vxrt ${DEPS_LIB})
