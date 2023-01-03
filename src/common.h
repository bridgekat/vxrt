#ifndef COMMON_H_
#define COMMON_H_

#include <cstdint>
#include <string>

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

using std::size_t;

// See: https://stackoverflow.com/questions/22346369/initialize-integer-literal-to-stdsize-t
constexpr auto operator"" _z(unsigned long long n) -> size_t { return n; }

inline auto ceilLog2(size_t x) -> size_t {
  auto res = 0_z;
  x--;
  while (x > 0) {
    x >>= 1;
    res++;
  }
  return res;
}

inline auto lowBit(size_t x) -> size_t { return x & (-x); }

// Paths.
inline auto rootPath() -> std::string { return "./"; }
inline auto configPath() -> std::string { return "./"; }
inline auto shaderPath() -> std::string { return "./Shaders/"; };
inline auto screenshotPath() -> std::string { return "./Screenshots/"; }
inline auto configFilename() -> std::string { return "Config.ini"; }

#endif // COMMON_H_
