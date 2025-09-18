cmake_minimum_required(VERSION 3.16)
project(light_scheduler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_executable(light_scheduler
  src/main.cpp
  src/app.cpp
  src/model.hpp
  src/time_util.cpp
  src/ini.cpp
  src/loader.cpp
  src/modbus_tcp.cpp
  src/scheduler.cpp
  src/interactive.cpp 
)

# Заголовки лежат в src/
target_include_directories(light_scheduler PRIVATE src)

if (WIN32)
  target_link_libraries(light_scheduler PRIVATE ws2_32)
endif()
