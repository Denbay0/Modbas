📌 Описание

Это эмулятор ПЛК c встроенным планировщиком освещения и сервером Modbus/TCP (slave).
SCADA (MasterSCADA) только задаёт параметры расписаний и флаги через Modbus; сам контроллер (эмулятор) включает/выключает «зонные» катушки по времени.

Без внешних библиотек. Windows, Winsock.

🧭 Карта адресов (offset с нуля)
Coils (катушки)
0.. — выходы зон (их включает/выключает планировщик).
500 + i — remote_enable для слота расписания i (0/1).

Holding-регистры
HR 0 — heartbeat (счётчик, +1 каждую секунду).
HR 1 — число слотов (только чтение), по умолчанию 16.
HR 2 — управление: бит0=1 → мягкая остановка эмулятора.
Слоты расписаний начинаются с HR 100, по 10 регистров на слот:

base = 100 + i*10
base+0 : enabled (0/1)
base+1 : type (0=weekly, 1=once)
base+2 : area_coil (offset катушки зоны)
base+3 : days_mask (бит0=Sun..бит6=Sat), weekly
base+4 : start_min (0..1439) = HH*60 + MM
base+5 : duration_min (>0)
base+6 : date_year  (YYYY), once
base+7 : date_month (1..12), once
base+8 : date_day   (1..31), once
base+9 : status (0=idle, 1=active, 2=consumed) — только чтение

⚙️ Сборка
cmake -S . -B build
cmake --build build --config Release

▶️ Запуск
.\build\Release\plc_sched_emulator.exe [порт] [unit_id]

