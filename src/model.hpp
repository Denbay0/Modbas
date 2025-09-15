#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <ctime>

enum class SchType { Weekly, Once };

struct Schedule {
    std::string id;
    bool enabled{true};
    std::optional<uint16_t> remote_enable_coil;
    SchType type{SchType::Weekly};
    std::string area;
    std::set<int> days_wday;          // 0=Sun..6=Sat
    int start_min{0};                  // HH:MM -> минуты с полуночи
    int duration_min{0};               // минуты
    std::optional<std::string> date_yyyy_mm_dd; // для once

    // runtime
    bool       active{false};
    std::time_t active_until{0};
    int        last_fire_yday{-1};     // weekly: чтобы не сработать 2 раза в день
    bool       consumed{false};        // once: после завершения больше не трогать
};

struct Config {
    std::string ip;
    uint16_t    port{502};
    uint8_t     unit_id{1};
    std::map<std::string,uint16_t> area_to_coil;
    std::optional<uint16_t> heartbeat_reg;
    int heartbeat_period{5};
    std::vector<Schedule> schedules;
};
