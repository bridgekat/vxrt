cmake_minimum_required (VERSION 3.15)

project (
  vxrt
  VERSION       0.1.0
  DESCRIPTION   "Voxel raycasting test."
  HOMEPAGE_URL  "https://github.com/bridgekat/vxrt"
  LANGUAGES     CXX
)

list (PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/extern/prelude/cmake")
include (Prelude)

# Set output directory.
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release/)

# ===== Dependencies =====
find_package (OpenGL REQUIRED)
find_package (GLEW REQUIRED)
find_package (SDL2 REQUIRED)

# ===== Targets =====
aux_source_directory            (src SOURCES)
add_executable                  (vxrt ${SOURCES})
target_default_compile_options  (vxrt)
target_include_directories      (vxrt PRIVATE src)
target_link_libraries           (vxrt PRIVATE OpenGL::GL GLEW::GLEW SDL2::SDL2)
