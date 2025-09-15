#include "time_util.hpp"
#include <stdexcept>
#include <cstdio>

std::pair<std::tm,std::time_t> LocalClock::now_tm() {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return {lt, t};
}

int LocalClock::hhmm_to_min(const std::string& hhmm) {
    int h=0,m=0;
    if (std::sscanf(hhmm.c_str(), "%d:%d", &h, &m) != 2)
        throw std::runtime_error("Bad time: "+hhmm);
    if (h<0||h>23||m<0||m>59)
        throw std::runtime_error("Bad time range: "+hhmm);
    return h*60+m;
}

void LocalClock::parse_date(const std::string& s, int& y, int& mo, int& d) {
    if (std::sscanf(s.c_str(), "%d-%d-%d", &y, &mo, &d) != 3)
        throw std::runtime_error("Bad date: "+s);
}

std::time_t LocalClock::make_local(int y,int mo,int d,int min_of_day) {
    std::tm tm{};
    tm.tm_year = y-1900; tm.tm_mon = mo-1; tm.tm_mday=d;
    tm.tm_hour = min_of_day/60; tm.tm_min = min_of_day%60; tm.tm_sec=0;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}
