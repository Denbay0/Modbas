#include "app.hpp"
#include "loader.hpp"
#include "time_util.hpp"
#include "scheduler.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

static std::atomic_bool g_stop{false};
static void on_signal(int){ g_stop = true; }

void App::try_connect() {
    for (;;) {
        try {
            mb_.close();
            mb_.connect_to(cfg_.ip, cfg_.port, cfg_.unit_id);
            std::cout<<"Connected to "<<cfg_.ip<<":"<<cfg_.port<<" uid="<<(int)cfg_.unit_id<<"\n";
            return;
        } catch (const std::exception& e) {
            std::cerr<<"Connect failed: "<<e.what()<<", retry in 1s...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

int App::run_with_config(Config cfg) {
    cfg_ = std::move(cfg);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    SchedulerEngine eng(cfg_, mb_, [this](){ this->try_connect(); });
    engine_ = &eng;

    try_connect();
    engine_->resume_after_restart();

    while(!g_stop) {
        engine_->step();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout<<"Stopping...\n";
    return 0;
}

int App::run(int argc, char** argv) {
    try {
        std::string cfg_path = (argc>1)? argv[1] : "config/config.ini";
        // обычный режим (ini)
        Config loaded = load_config_ini(cfg_path);
        return run_with_config(std::move(loaded));
    } catch (const std::exception& e) {
        std::cerr<<"Fatal: "<<e.what()<<"\n";
        return 1;
    }
}
