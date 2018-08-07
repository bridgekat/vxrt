#ifndef LOGGER_H_
#define LOGGER_H_

#include <iostream>
#include <string>
#include <sstream>

#define LOG(Message, OutStream, Level) OutStream << Level << Message << std::endl;
inline void LogVerbose(const std::string& message) { LOG(message, std::cout, "[Verbose] "); }
inline void LogInfo(const std::string& message) { LOG(message, std::cout, "[Info] "); }
inline void LogWarning(const std::string& message) { LOG(message, std::cout, "[Warning] "); }
inline void LogError(const std::string& message) { LOG(message, std::cout, "[Error] "); }
inline void LogFatal(const std::string& message) { LOG(message, std::cout, "[Fatal] "); }
#undef LOG

#endif

