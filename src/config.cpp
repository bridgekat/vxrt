#include "config.h"
#include <fstream>
#include "logger.h"

using std::string;

std::map<string, string> Config::mConfigs, Config::mDefaults;

string trim(const string& s) {
	int l = 0, r = int(s.size()) - 1;
	while (l < int(s.size()) && string(" \r\n\t").find(s[l]) != string::npos) l++;
	while (r >= 0 && string(" \r\n\t").find(s[r]) != string::npos) r--;
	if (l > r) return "";
	return s.substr(l, r - l + 1);
}

std::pair<string, string> parse(string s) {
	size_t pos = s.find('#');
	if (pos != string::npos) s = s.substr(0, pos);
	pos = s.find('=');
	if (pos == string::npos) return std::pair<string, string>();
	return std::make_pair(trim(s.substr(0, pos)), trim(s.substr(pos + 1)));
}

void Config::load(const string& filename) {
	mConfigs.clear();
	std::ifstream file(filename);
	if (!file.is_open()) {
		return;
	}
	while (!file.eof()) {
		string s;
		std::getline(file, s);
		std::pair<string, string> res = parse(s);
		if (res.first != "") mConfigs[res.first] = res.second;
	}
}

void Config::save(const string& filename) {
	std::ofstream file(filename);
	if (!file.is_open()) {
		LogWarning("Unable to access file \"" + filename + "\" for saving configs");
		return;
	}
	std::map<string, string> out = mDefaults;
	for (auto& s: mConfigs) out[s.first] = s.second;
	for (auto& s: out) file << s.first << " = " << s.second << std::endl;
}

