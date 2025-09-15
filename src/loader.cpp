#include "loader.hpp"
#include "ini.hpp"
#include "time_util.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

static inline std::string trim(std::string s) {
    auto issp = [](unsigned char c){ return std::isspace(c); };
    while(!s.empty() && issp(s.front())) s.erase(s.begin());
    while(!s.empty() && issp(s.back()))  s.pop_back();
    return s;
}
static inline std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::stringstream ss(s); std::string it;
    while (std::getline(ss,it,d)) out.push_back(it);
    return out;
}
static inline bool ieq(const std::string& a, const std::string& b) {
    if (a.size()!=b.size()) return false;
    for (size_t i=0;i<a.size();++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

int day_to_wday(const std::string& s0){
    std::string s=s0; std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s=="sun"||s=="su"||s=="sunday") return 0;
    if (s=="mon"||s=="mo"||s=="monday") return 1;
    if (s=="tue"||s=="tu"||s=="tuesday") return 2;
    if (s=="wed"||s=="we"||s=="wednesday") return 3;
    if (s=="thu"||s=="th"||s=="thursday") return 4;
    if (s=="fri"||s=="fr"||s=="friday") return 5;
    if (s=="sat"||s=="sa"||s=="saturday") return 6;
    throw std::runtime_error("Unknown day: "+s0);
}

static uint16_t to_u16(const std::string& s) {
    int v = std::stoi(s);
    if (v<0 || v>65535) throw std::runtime_error("u16 out of range: "+s);
    return (uint16_t)v;
}

Config load_config_ini(const std::string& path) {
    Ini ini = read_ini(path);
    Config c;

    // PLC
    if (ini.sec.count("PLC")) {
        auto& S = ini.sec["PLC"];
        c.ip = S.count("ip")? S["ip"] : "127.0.0.1";
        c.port = (uint16_t)(S.count("port")? std::stoi(S["port"]) : 502);
        c.unit_id = (uint8_t)(S.count("unit_id")? std::stoi(S["unit_id"]) : 1);
    } else throw std::runtime_error("[PLC] section required");

    // Areas
    if (ini.sec.count("Areas")) {
        for (auto& kv : ini.sec["Areas"]) c.area_to_coil[kv.first] = to_u16(kv.second);
    } else throw std::runtime_error("[Areas] section required");

    // Heartbeat
    if (ini.sec.count("Heartbeat")) {
        auto& H = ini.sec["Heartbeat"];
        if (H.count("holding")) c.heartbeat_reg = to_u16(H["holding"]);
        if (H.count("period"))  c.heartbeat_period = std::max(1, std::stoi(H["period"]));
    }

    // Schedules
    for (auto& kv : ini.schedules) {
        (void)kv.first;
        std::string v = kv.second;
        Schedule s;

        for (auto& p : split(v, ';')) {
            auto t = trim(p);
            if (t.empty()) continue;
            auto eq = t.find('=');
            if (eq==std::string::npos) continue;
            auto key = trim(t.substr(0,eq));
            auto val = trim(t.substr(eq+1));

            if (ieq(key,"id")) s.id = val;
            else if (ieq(key,"type")) {
                if (ieq(val,"weekly")) s.type = SchType::Weekly;
                else if (ieq(val,"once"))  s.type = SchType::Once;
                else throw std::runtime_error("Unknown type: "+val);
            }
            else if (ieq(key,"enabled")) s.enabled = (val!="0");
            else if (ieq(key,"area")) s.area = val;
            else if (ieq(key,"days")) {
                for (auto& d : split(val, ',')) s.days_wday.insert(day_to_wday(trim(d)));
            }
            else if (ieq(key,"start")) s.start_min = LocalClock::hhmm_to_min(val);
            else if (ieq(key,"duration")) s.duration_min = std::stoi(val);
            else if (ieq(key,"date")) s.date_yyyy_mm_dd = val;
            else if (ieq(key,"time")) s.start_min = LocalClock::hhmm_to_min(val);
            else if (ieq(key,"remote_enable_coil")) s.remote_enable_coil = to_u16(val);
        }

        if (s.id.empty()) throw std::runtime_error("schedule without id");
        if (s.area.empty()) throw std::runtime_error("schedule "+s.id+" without area");
        if (s.duration_min<=0) throw std::runtime_error("schedule "+s.id+" bad duration");
        if (s.type==SchType::Weekly) {
            if (s.days_wday.empty()) throw std::runtime_error("schedule "+s.id+" no days");
        } else {
            if (!s.date_yyyy_mm_dd) throw std::runtime_error("schedule "+s.id+" needs date");
        }
        c.schedules.push_back(std::move(s));
    }

    return c;
}
