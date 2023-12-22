cmake_minimum_required (VERSION 3.28)
cmake_policy (SET CMP0135 NEW)

project (vxrt
  VERSION       0.1.0
  DESCRIPTION   "Voxel raycasting test."
  HOMEPAGE_URL  "https://github.com/bridgekat/vxrt"
  LANGUAGES     CXX
)

# ===== Scripts =====
list (PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include (FetchContent)
include (TargetDefaultCompileOptions)
include (CppcheckCodeAnalysis)

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
target_compile_definitions      (vxrt PRIVATE SDL_MAIN_HANDLED)
