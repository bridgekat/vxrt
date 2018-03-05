#ifndef CONFIG_H_
#define CONFIG_H_

#include <string>
#include <map>

#include "common.h"
#include <sstream>

class Config {
public:
	using string = std::string;

	static void setDefaultString(const string& name, const string& value) {
		mDefaults[name] = value;
	}

	static void setDefaultInt(const string& name, int value) {
		std::stringstream ss;
		ss << value;
		mDefaults[name] = ss.str();
	}

	static void setDefaultDouble(const string& name, double value) {
		std::stringstream ss;
		ss << value;
		mDefaults[name] = ss.str();
	}

	static string getString(const string& name, const string& value = "") {
		if (value != "") setDefaultString(name, value);
		auto it = mConfigs.find(name);
		return it != mConfigs.end() ? it->second : mDefaults[name];
	}

	static int getInt(const string& name, int value = 0) {
		if (value != 0) setDefaultInt(name, value);
		std::stringstream ss(getString(name));
		int res = 0;
		ss >> res;
		return res;
	}

	static double getDouble(const string& name, double value = 0.0) {
		if (value != 0.0) setDefaultDouble(name, value);
		std::stringstream ss(getString(name));
		double res = 0.0;
		ss >> res;
		return res;
	}

	static void load(const string& file = string(ConfigPath) + ConfigFilename);
	static void save(const string& file = string(ConfigPath) + ConfigFilename);

private:
	static std::map<string, string> mConfigs, mDefaults;
};

#endif

