#ifndef CONFIG_H_
#define CONFIG_H_

#include <concepts>
#include <map>
#include <sstream>
#include <string>
#include "common.h"

template <typename T>
concept StringStreamConvertible = requires (T a, std::stringstream ss) {
  { ss << a };
  { ss >> a };
};

class Config {
public:
  // Writes default value to config if key is not present.
  template <typename T>
  requires std::default_initializable<T> && StringStreamConvertible<T>
  T getOr(std::string const& key, T value) {
    if (auto const it = mValues.find(key); it != mValues.end()) {
      T res;
      std::stringstream ss(it->second);
      ss >> res;
      return res;
    } else {
      std::stringstream ss;
      ss << value;
      mValues[key] = ss.str();
      return value;
    }
  }

  void load(std::string const& file);
  void save(std::string const& file);

private:
  std::map<std::string, std::string> mValues;
};

static_assert(std::move_constructible<Config>);
static_assert(std::assignable_from<Config&, Config&&>);
static_assert(std::copy_constructible<Config>);
static_assert(std::assignable_from<Config&, Config&>);

#endif // CONFIG_H_
