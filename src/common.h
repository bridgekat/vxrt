#ifndef COMMON_H_
#define COMMON_H_

#include <cstdint>

// Compiler.
#ifdef _MSC_VER
#  define VXRT_COMPILER_MSVC
#endif

// Debugging.
#ifdef _DEBUG
#  define VXRT_DEBUG
#endif

// Target.
#ifdef _WIN32
#  define VXRT_TARGET_WINDOWS
#elif defined __MACOSX__ || (defined __APPLE__ && defined __GNUC__)
#  define VXRT_TARGET_MACOSX
#  define VXRT_TARGET_POSIX
#else
#  define VXRT_TARGET_LINUX
#  define VXRT_TARGET_POSIX
#endif

constexpr char const* RootPath = "./";
constexpr char const* ConfigPath = "./";
constexpr char const* ShaderPath = "./Shaders/";
constexpr char const* ScreenshotPath = "./Screenshots/";
constexpr char const* ConfigFilename = "Config.ini";

inline size_t ceilLog2(size_t x) {
  size_t res = 0;
  x--;
  while (x > 0) {
    x >>= 1;
    res++;
  }
  return res;
}

inline size_t lowBit(size_t x) { return x & (-x); }

#endif // COMMON_H_
