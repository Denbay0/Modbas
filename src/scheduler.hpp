#pragma once
#include "model.hpp"
#include "modbus_tcp.hpp"
#include <atomic>
#include <chrono>
#include <functional>

class SchedulerEngine {
public:
    SchedulerEngine(Config& cfg, ModbusTcpClient& mb, std::function<void()> reconnect);

    // Вызвать один раз после подключения (восстанавливает активные окна)
    void resume_after_restart();

    // Один «тик» (1 раз в секунду): heartbeat + логика расписаний
    void step();

private:
    bool effective_enabled(const Schedule& s);
    void switch_on (Schedule& s, uint16_t coil);
    void switch_off(Schedule& s, uint16_t coil);

private:
    Config& cfg_;
    ModbusTcpClient& mb_;
    std::function<void()> reconnect_;
    uint16_t hb_{0};
    std::chrono::steady_clock::time_point last_hb_{std::chrono::steady_clock::now()};
};
