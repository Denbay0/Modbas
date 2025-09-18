#include "app.hpp"
#include "interactive.hpp"
#include <string>

int main(int argc, char** argv) {
    App app;
    if (argc >= 2 && std::string(argv[1]) == "--interactive") {
        try {
            Config cfg = build_config_interactive();
            return app.run_with_config(std::move(cfg));
        } catch (const std::exception& e) {
            fprintf(stderr, "Interactive error: %s\n", e.what());
            return 1;
        }
    }
    return app.run(argc, argv);
}
