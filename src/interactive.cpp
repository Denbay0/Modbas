#include "interactive.hpp"
#include "time_util.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string ask_str(const std::string& prompt, const std::string& def="") {
    std::string s;
    std::cout << prompt;
    if (!def.empty()) std::cout << " [" << def << "]";
    std::cout << ": ";
    std::getline(std::cin, s);
    if (s.empty()) s = def;
    return s;
}
static int ask_int(const std::string& prompt, int def) {
    for (;;) {
        std::string s = ask_str(prompt, std::to_string(def));
        try { return std::stoi(s); }
        catch (...) { std::cout << "Enter a number.\n"; }
    }
}
static bool ask_yesno(const std::string& prompt, bool def) {
    for(;;){
        std::string s = ask_str(prompt, def? "y":"n");
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s.empty()) return def;
        if (s=="y"||s=="yes") return true;
        if (s=="n"||s=="no")  return false;
        std::cout<<"Enter y/n.\n";
    }
}
static void trim(std::string& s){
    auto issp=[](unsigned char c){return std::isspace(c);};
    while(!s.empty() && issp(s.front())) s.erase(s.begin());
    while(!s.empty() && issp(s.back()))  s.pop_back();
}
static std::vector<std::string> split_csv(const std::string& s){
    std::vector<std::string> out; std::stringstream ss(s); std::string it;
    while(std::getline(ss, it, ',')){ trim(it); if(!it.empty()) out.push_back(it); }
    return out;
}

Config build_config_interactive() {
    Config cfg;

    // PLC
    cfg.ip      = ask_str("PLC IP", "127.0.0.1");
    cfg.port    = (uint16_t)ask_int("PLC port", 1502);
    cfg.unit_id = (uint8_t) ask_int("PLC unit_id", 1);

    // Areas
    std::cout << "\n=== Areas (name -> coil offset) ===\n";
    for (;;) {
        std::string name = ask_str("Area name (empty to finish)");
        if (name.empty()) break;
        int offset = ask_int("Coil offset (0-based)", 0);
        if (offset < 0 || offset > 65535) { std::cout << "Range 0..65535\n"; continue; }
        cfg.area_to_coil[name] = (uint16_t)offset;
    }
    if (cfg.area_to_coil.empty())
        throw std::runtime_error("At least one area is required.");

    // Heartbeat
    std::cout << "\n=== Heartbeat ===\n";
    int hb = ask_int("Holding register offset (40001 -> 0, -1 to disable)", 0);
    if (hb >= 0) cfg.heartbeat_reg = (uint16_t)hb;
    cfg.heartbeat_period = ask_int("Heartbeat period, seconds", 5);

    // Schedules
    std::cout << "\n=== Schedules ===\n";
    for (int idx=1;;++idx) {
        if (!ask_yesno("Add a schedule?", idx==1)) break;

        Schedule s;
        s.id      = ask_str("Schedule ID", "ev"+std::to_string(idx));
        s.enabled = ask_yesno("enabled?", true);

        std::string area = ask_str("area (one of declared)");
        if (!cfg.area_to_coil.count(area)) {
            std::cout << "Unknown area. Available: ";
            for (auto& kv: cfg.area_to_coil) std::cout << kv.first << " ";
            std::cout << "\n"; continue;
        }
        s.area = area;

        // type
        std::string t = ask_str("type weekly/once", "weekly");
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t=="weekly") {
            s.type = SchType::Weekly;
            auto days = split_csv(ask_str("days (Mon,Tue,Wed,Thu,Fri,Sat,Sun)", "Mon,Tue,Wed,Thu,Fri"));
            auto to_wday = [](const std::string& d)->int{
                std::string x=d; std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                if (x=="sun"||x=="su"||x=="sunday") return 0;
                if (x=="mon"||x=="mo"||x=="monday") return 1;
                if (x=="tue"||x=="tu"||x=="tuesday") return 2;
                if (x=="wed"||x=="we"||x=="wednesday") return 3;
                if (x=="thu"||x=="th"||x=="thursday") return 4;
                if (x=="fri"||x=="fr"||x=="friday") return 5;
                if (x=="sat"||x=="sa"||x=="saturday") return 6;
                throw std::runtime_error("Unknown day: "+d);
            };
            for (auto& d: days) s.days_wday.insert(to_wday(d));
            s.start_min    = LocalClock::hhmm_to_min(ask_str("start (HH:MM)", "08:00"));
            s.duration_min = ask_int("duration (minutes)", 60);
        } else if (t=="once") {
            s.type = SchType::Once;
            s.date_yyyy_mm_dd = ask_str("date (YYYY-MM-DD)");
            s.start_min       = LocalClock::hhmm_to_min(ask_str("time (HH:MM)"));
            s.duration_min    = ask_int("duration (minutes)", 30);
        } else {
            std::cout << "Unknown type.\n"; continue;
        }

        // optional remote_enable_coil
        int rc = ask_int("remote_enable_coil offset (-1 to skip)", -1);
        if (rc >= 0) s.remote_enable_coil = (uint16_t)rc;

        cfg.schedules.push_back(std::move(s));
    }

    if (cfg.schedules.empty())
        throw std::runtime_error("At least one schedule is required.");

    // Summary
    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "PLC " << cfg.ip << ":" << cfg.port << " uid=" << (int)cfg.unit_id << "\n";
    std::cout << "Areas:\n";
    for (auto& kv: cfg.area_to_coil) std::cout << "  " << kv.first << " -> " << kv.second << "\n";
    if (cfg.heartbeat_reg) std::cout << "Heartbeat HR@" << *cfg.heartbeat_reg << " / " << cfg.heartbeat_period << "s\n";
    else                   std::cout << "Heartbeat: OFF\n";
    std::cout << "Schedules: " << cfg.schedules.size() << "\n";
    for (auto& s: cfg.schedules) {
        std::cout << "  ["<<s.id<<"] "<<(s.enabled?"EN":"DIS")<<" area="<<s.area<<" type="
                  <<(s.type==SchType::Weekly?"weekly":"once")
                  <<(s.remote_enable_coil? " rem="+std::to_string(*s.remote_enable_coil):"")
                  <<"\n";
    }
    if (!ask_yesno("Start with these settings?", true))
        throw std::runtime_error("Cancelled by user.");

    return cfg;
}
