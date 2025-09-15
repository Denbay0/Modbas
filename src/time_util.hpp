#pragma once
#include <ctime>
#include <string>
#include <utility>

struct LocalClock {
    static std::pair<std::tm,std::time_t> now_tm();
    static int  hhmm_to_min(const std::string& hhmm);
    static void parse_date(const std::string& s, int& y, int& mo, int& d);
    static std::time_t make_local(int y,int mo,int d,int min_of_day);
};

