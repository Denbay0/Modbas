#include "ini.hpp"
#include <fstream>
#include <sstream>
#include <cctype>
#include <stdexcept>

static inline std::string trim(std::string s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    while(!s.empty() && issp(s.front())) s.erase(s.begin());
    while(!s.empty() && issp(s.back()))  s.pop_back();
    return s;
}

Ini read_ini(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open config: "+path);
    Ini ini;
    std::string line, cur;
    while (std::getline(f,line)) {
        line = trim(line);
        if (line.empty() || line[0]==';' || line[0]=='#') continue;
        if (line.front()=='[' && line.back()==']') {
            cur = line.substr(1, line.size()-2);
            continue;
        }
        auto pos = line.find('=');
        if (pos==std::string::npos) continue;
        std::string k = trim(line.substr(0,pos));
        std::string v = trim(line.substr(pos+1));
        if (cur=="Schedule") ini.schedules.emplace_back(k, v);
        else ini.sec[cur][k]=v;
    }
    return ini;
}
