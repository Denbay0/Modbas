cmake_minimum_required(VERSION 3.16)
project(plc_sched_emulator LANGUAGES CXX)

# Стандарт и предупреждения
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Исполняемый файл ЭМУЛЯТОРА КОНТРОЛЛЕРА (Modbus/TCP SLAVE + планировщик)
add_executable(plc_sched_emulator
  src/plc_sched_emulator.cpp
)

# Инклуды (если файл лежит в src/)
target_include_directories(plc_sched_emulator PRIVATE src)

# Линкуем Winsock на Windows
if (WIN32)
  target_link_libraries(plc_sched_emulator PRIVATE ws2_32)
endif()

