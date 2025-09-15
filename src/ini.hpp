#pragma once
#include <map>
#include <string>
#include <vector>

struct Ini {
    std::map<std::string, std::map<std::string,std::string>> sec;
    std::vector<std::pair<std::string,std::string>> schedules; // [Schedule]: key -> value(line)
};

Ini read_ini(const std::string& path);
