#ifndef LOG_H_
#define LOG_H_

#include <iostream>
#include <sstream>
#include <string>

class Log {
public:
#define LOG(msg, ostream, level) ostream << level << msg << std::endl;
  static void verbose(std::string const& msg) { LOG(msg, std::cout, "[Verbose] "); }
  static void info(std::string const& msg) { LOG(msg, std::cout, "[Info] "); }
  static void warning(std::string const& msg) { LOG(msg, std::cerr, "[Warning] "); }
  static void error(std::string const& msg) { LOG(msg, std::cerr, "[Error] "); }
  static void fatal(std::string const& msg) { LOG(msg, std::cerr, "[Fatal] "); }
#undef LOG
};

#endif // LOG_H_
