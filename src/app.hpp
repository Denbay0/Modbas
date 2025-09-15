#pragma once
#include "model.hpp"
#include "modbus_tcp.hpp"
#include "scheduler.hpp"
#include <string>

class App {
public:
    int run(int argc, char** argv);

private:
    void try_connect();

private:
    Config cfg_;
    ModbusTcpClient mb_;
    SchedulerEngine* engine_{nullptr}; // создадим после загрузки cfg_
};
