#pragma once
#include "model.hpp"

// Мастер ввода с клавиатуры. Возвращает собранный Config.
// Бросает std::exception при отмене/ошибке.
Config build_config_interactive();
