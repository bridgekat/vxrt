#ifndef COMMON_H_
#define COMMON_H_

// Compiler
#ifdef _MSC_VER
#	define VXRT_COMPILER_MSVC
#endif

// Debugging
#ifdef _DEBUG
#	define VXRT_DEBUG
#endif

// Target
#ifdef _WIN32
#	define VXRT_TARGET_WINDOWS
#elif defined __MACOSX__ || (defined __APPLE__ && defined __GNUC__)
#	define VXRT_TARGET_MACOSX
#	define VXRT_TARGET_POSIX
#else
#	define VXRT_TARGET_LINUX
#	define VXRT_TARGET_POSIX
#endif

constexpr const char* RootPath = "./";
constexpr const char* ConfigPath = "./";
constexpr const char* ShaderPath = "./Shaders/";
constexpr const char* ConfigFilename = "Config.ini";

#endif // !COMMON_H_

