#include "config.h"
#include <fstream>
#include <string_view>
#include "log.h"

using std::string;
using std::string_view;

constexpr string_view Whitespace = " \r\n\t\v\f";

string trim(string const& s) {
  size_t l = 0, r = s.size();
  while (l < s.size() && Whitespace.find(s[l]) != string::npos)
    l++;
  while (r > 0 && Whitespace.find(s[r - 1]) != string::npos)
    r--;
  if (l >= r)
    return "";
  return s.substr(l, r - l);
}

std::pair<string, string> split(string s) {
  size_t pos = s.find('#');
  if (pos != string::npos)
    s = s.substr(0, pos);
  pos = s.find('=');
  if (pos == string::npos)
    return std::pair<string, string>();
  return std::make_pair(trim(s.substr(0, pos)), trim(s.substr(pos + 1)));
}

void Config::load(string const& filename) {
  mValues.clear();
  std::ifstream file(filename);
  if (!file.is_open()) {
    Log::info("Config::load(): cannot open file \"" + filename + "\", initialising.");
    return;
  }
  while (!file.eof()) {
    string s;
    std::getline(file, s);
    std::pair<string, string> res = split(s);
    if (res.first != "" && res.second != "")
      mValues[res.first] = res.second;
  }
}

void Config::save(string const& filename) {
  std::ofstream file(filename);
  if (!file.is_open()) {
    Log::warning("Config::save(): cannot open file \"" + filename + "\".");
    return;
  }
  for (auto& s: mValues)
    file << s.first << " = " << s.second << std::endl;
}
