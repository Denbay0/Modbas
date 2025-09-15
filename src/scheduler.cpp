#include "scheduler.hpp"
#include "time_util.hpp"
#include <iostream>

SchedulerEngine::SchedulerEngine(Config& cfg, ModbusTcpClient& mb, std::function<void()> reconnect)
: cfg_(cfg), mb_(mb), reconnect_(std::move(reconnect)) {}

bool SchedulerEngine::effective_enabled(const Schedule& s) {
    bool rem_en = true;
    if (s.remote_enable_coil) {
        try {
            auto rv = mb_.read_coil(*s.remote_enable_coil);
            if (rv) rem_en = *rv;
        } catch (...) { reconnect_(); }
    }
    return s.enabled && rem_en;
}

void SchedulerEngine::switch_on(Schedule& s, uint16_t coil) {
    try {
        if (mb_.write_coil(coil, true)) {
            s.active = true;
            std::cout<<"[ON] "<<s.id<<" area="<<s.area<<" coil="<<coil<<"\n";
        }
    } catch (...) { reconnect_(); }
}

void SchedulerEngine::switch_off(Schedule& s, uint16_t coil) {
    try {
        if (mb_.write_coil(coil, false)) {
            s.active = false;
            std::cout<<"[OFF] "<<s.id<<" area="<<s.area<<" coil="<<coil<<"\n";
        }
    } catch (...) { reconnect_(); }
}

void SchedulerEngine::resume_after_restart() {
    auto [lt, now] = LocalClock::now_tm();
    int tod = lt.tm_hour*60 + lt.tm_min;

    for (auto& s : cfg_.schedules) {
        if (!effective_enabled(s)) continue;
        auto it = cfg_.area_to_coil.find(s.area);
        if (it == cfg_.area_to_coil.end()) continue;
        uint16_t coil = it->second;

        if (s.type == SchType::Weekly) {
            if (s.days_wday.count(lt.tm_wday)) {
                int end_min = s.start_min + s.duration_min;
                if (s.start_min <= tod && tod < end_min) {
                    switch_on(s, coil);
                    s.active_until  = std::time(nullptr) + (end_min - tod)*60;
                    s.last_fire_yday = lt.tm_yday;
                    std::cout<<"[INIT] resume "<<s.id<<"\n";
                }
            }
        } else {
            int y,mo,d; LocalClock::parse_date(*s.date_yyyy_mm_dd,y,mo,d);
            std::time_t st = LocalClock::make_local(y,mo,d, s.start_min);
            std::time_t en = st + s.duration_min*60;
            if (now >= st && now < en) {
                switch_on(s, coil);
                s.active_until = en;
                std::cout<<"[INIT] resume ONCE "<<s.id<<"\n";
            } else if (now >= en) {
                s.consumed = true;
            }
        }
    }
}

void SchedulerEngine::step() {
    auto [lt, now] = LocalClock::now_tm();
    int tod = lt.tm_hour*60 + lt.tm_min;

    // Heartbeat
    if (cfg_.heartbeat_reg) {
        auto t = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(t - last_hb_).count() >= cfg_.heartbeat_period) {
            try { mb_.write_holding(*cfg_.heartbeat_reg, hb_++); last_hb_ = t; }
            catch (...) { reconnect_(); }
        }
    }

    for (auto& s : cfg_.schedules) {
        auto it = cfg_.area_to_coil.find(s.area);
        if (it == cfg_.area_to_coil.end()) continue;
        uint16_t coil = it->second;

        bool enabled = effective_enabled(s);

        if (!enabled) {
            if (s.active) switch_off(s, coil);
            continue;
        }

        if (s.type == SchType::Weekly) {
            if (s.days_wday.count(lt.tm_wday)) {
                if (tod == s.start_min && s.last_fire_yday != lt.tm_yday && !s.active) {
                    switch_on(s, coil);
                    s.active_until   = now + s.duration_min*60;
                    s.last_fire_yday = lt.tm_yday;
                }
            } else {
                s.last_fire_yday = -1;
            }
            if (s.active && now >= s.active_until) {
                switch_off(s, coil);
            }
        } else {
            if (s.consumed) continue;
            int y,mo,d; LocalClock::parse_date(*s.date_yyyy_mm_dd,y,mo,d);
            std::time_t st = LocalClock::make_local(y,mo,d, s.start_min);
            std::time_t en = st + s.duration_min*60;

            if (!s.active && now >= st && now < en) {
                switch_on(s, coil);
                s.active_until = en;
            }
            if (s.active && now >= en) {
                switch_off(s, coil);
                s.consumed = true;
            }
        }
    }
}
