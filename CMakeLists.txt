cmake_minimum_required (VERSION 3.12)
cmake_policy (SET CMP0135 NEW)

project (vxrt VERSION 0.1 LANGUAGES CXX)

option (VXRT_ENABLE_IPO "Enable inter-procedural optimizations" OFF)
option (VXRT_ENABLE_CODE_ANALYSIS "Run code analysis with cppcheck" OFF)
option (VXRT_ENABLE_MOST_WARNINGS "Enable most warnings" OFF)
option (VXRT_ENABLE_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option (VXRT_ENABLE_GLIBCXX_DEBUG_CHECKS "Enable GLIBCXX debug checks at debug configuration" ON)
option (VXRT_ENABLE_ASAN "Enable ASan (MSVC/GCC/Clang)" OFF)
option (VXRT_ENABLE_UBSAN "Enable UBSan (GCC/Clang)" OFF)
option (VXRT_LD_EXTEND_STACK "Extend stack memory to 10MB (Linux LD)" OFF)
option (VXRT_MACOS_LD_EXTEND_STACK "Extend stack memory to 10MB (macOS LD)" OFF)
option (VXRT_LINK_EXTEND_STACK "Extend stack memory to 10MB (MSVC LINK)" OFF)

# See: https://cliutils.gitlab.io/modern-cmake/chapters/features/small.html
include (CheckIPOSupported)
check_ipo_supported (RESULT IPO_SUPPORTED)
if (IPO_SUPPORTED AND VXRT_ENABLE_IPO)
  message (STATUS "IPO/LTO ENABLED")
endif ()

# See: https://stackoverflow.com/questions/56957172/cmake-conditionally-run-cppcheck
if (VXRT_ENABLE_CODE_ANALYSIS)
  find_program (cppcheck cppcheck)
  message (STATUS "cppcheck ${cppcheck}")
  if (NOT (cppcheck MATCHES "NOTFOUND"))
    # Notes:
    # - unmatchedSuppression needed since not all source files are analyzed simultaneously
    # - unusedFunction needed since not all source files are analyzed simultaneously
    # - template needed to force `warning` into output string, and to make `id` available for suppression
    set (CMAKE_CXX_CPPCHECK "${cppcheck}"
      "--enable=all"
      "--inconclusive"
      "--inline-suppr"
      "--quiet"
      "--suppress=unmatchedSuppression"
      "--suppress=unusedFunction"
      "--template='{file}:{line}: warning: {id} ({severity}): {message}'")
  endif ()
endif ()

# Set default compile options.
macro (target_default_compile_options TARGET)
  target_compile_features     (${TARGET} PRIVATE cxx_std_20)
  set_target_properties       (${TARGET} PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_definitions  (${TARGET} PRIVATE "SDL_MAIN_HANDLED")
  if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options    (${TARGET} PRIVATE "/MP" "/EHsc")
  endif ()
  if (VXRT_ENABLE_MOST_WARNINGS)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options  (${TARGET} PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Wdeprecated" "-Wconversion" "-Wsign-conversion" "-Werror=old-style-cast")
      target_compile_options  (${TARGET} PRIVATE "-Weffc++")
      target_compile_options  (${TARGET} PRIVATE "-Wno-nested-anon-types")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options  (${TARGET} PRIVATE "-Wall" "-Wextra" "-Wpedantic" "-Wdeprecated" "-Wconversion" "-Wsign-conversion" "-Werror=old-style-cast")
      target_compile_options  (${TARGET} PRIVATE "-Wno-nested-anon-types")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      target_compile_options  (${TARGET} PRIVATE "/W4" "/permissive-")
    endif ()
  endif ()
  if (VXRT_ENABLE_WARNINGS_AS_ERRORS)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      target_compile_options  (${TARGET} PRIVATE "-Werror")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      target_compile_options  (${TARGET} PRIVATE "-Werror")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
      target_compile_options  (${TARGET} PRIVATE "/WX")
    endif ()
  endif ()
  if (VXRT_ENABLE_GLIBCXX_DEBUG_CHECKS)
    target_compile_options    (${TARGET} PRIVATE "$<$<CONFIG:Debug>:-D_GLIBCXX_CONCEPT_CHECKS>")
    target_compile_options    (${TARGET} PRIVATE "$<$<CONFIG:Debug>:-D_GLIBCXX_DEBUG>")
    target_compile_options    (${TARGET} PRIVATE "$<$<CONFIG:Debug>:-D_GLIBCXX_DEBUG_PEDANTIC>")
  endif ()
  if (VXRT_ENABLE_ASAN)
    target_compile_options    (${TARGET} PRIVATE "-fsanitize=address")
  endif ()
  if (VXRT_ENABLE_UBSAN)
    target_compile_options    (${TARGET} PRIVATE "-fsanitize=undefined")
  endif ()
  if (VXRT_LD_EXTEND_STACK)
    set_target_properties     (${TARGET} PROPERTIES LINK_FLAGS "-Wl,-z,stack-size=10000000")
  endif ()
  if (VXRT_MACOS_LD_EXTEND_STACK)
    set_target_properties     (${TARGET} PROPERTIES LINK_FLAGS "-Wl,-stack_size,10000000")
  endif ()
  if (VXRT_LINK_EXTEND_STACK)
    set_target_properties     (${TARGET} PROPERTIES LINK_FLAGS "/STACK:10000000")
  endif ()
  if (IPO_SUPPORTED AND VXRT_ENABLE_IPO)
    set_target_properties     (${TARGET} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif ()
endmacro ()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/release/)

find_package (OpenGL REQUIRED)
find_package (GLEW REQUIRED)
find_package (SDL2 REQUIRED)

# Add targets.
aux_source_directory (./src/ SOURCES)
add_executable                  (vxrt ${SOURCES})
target_default_compile_options  (vxrt)
target_include_directories      (vxrt PRIVATE src)
target_link_libraries           (vxrt PRIVATE OpenGL::GL GLEW::GLEW SDL2::SDL2)
