// src/plc_sched_emulator.cpp
// Modbus/TCP "controller" emulator with built-in scheduler (Variant B).
// SCADA writes schedule params into holding/coils; the emulator runs the schedule
// and toggles area output coils accordingly.
//
// Build (MSVC Dev Prompt):
//   cl /EHsc /W4 src\\plc_sched_emulator.cpp ws2_32.lib /Fe:plc_sched_emulator.exe
//
// Run:
//   plc_sched_emulator.exe [port] [unit_id]
// Defaults: port=1502, unit_id=1
//
// Register/Coil Map (0-based offsets):
// Coils:
//   0..N-1            : area output coils (scheduler sets ON/OFF)
//   500 + i           : remote_enable flag for schedule slot i (0/1)
//
// Holding Registers:
//   0                 : heartbeat counter (increments each second)
//   1                 : number of schedule slots (read-only)
//   100 + i*10 + 0    : enabled (0/1)
//   100 + i*10 + 1    : type (0=weekly,1=once)
//   100 + i*10 + 2    : area_coil (coil offset to toggle)
//   100 + i*10 + 3    : days_mask (bit0=Sun..bit6=Sat), weekly only
//   100 + i*10 + 4    : start_min (0..1439)
//   100 + i*10 + 5    : duration_min (>0)
//   100 + i*10 + 6    : date_year (YYYY), once only
//   100 + i*10 + 7    : date_month (1..12), once only
//   100 + i*10 + 8    : date_day (1..31), once only
//   100 + i*10 + 9    : status (0=idle,1=active,2=consumed) [read-only]

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <csignal>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

// ---- Limits
static const int MAX_COILS = 4096;
static const int MAX_HOLDS = 8192;
static const int MAX_SLOTS = 16;

// ---- Map constants
static const uint16_t COIL_AREA_BASE      = 0;
static const uint16_t COIL_REMOTE_EN_BASE = 500;

static const uint16_t HR_HEARTBEAT        = 0;
static const uint16_t HR_NUM_SLOTS        = 1;
static const uint16_t HR_SCHED_BASE       = 100;
static const uint16_t HR_SCHED_STRIDE     = 10;

// schedule fields within a slot
enum : uint16_t {
    F_ENABLED = 0,
    F_TYPE    = 1,  // 0 weekly, 1 once
    F_AREA    = 2,  // coil offset
    F_DAYS    = 3,  // bit0=Sun..bit6=Sat
    F_START   = 4,  // minutes of day
    F_DUR     = 5,  // minutes
    F_YEAR    = 6,  // YYYY (once)
    F_MONTH   = 7,  // 1..12 (once)
    F_DAY     = 8,  // 1..31 (once)
    F_STATUS  = 9   // 0 idle,1 active,2 consumed (read-only)
};

static uint8_t  g_coils[MAX_COILS];
static uint16_t g_holds[MAX_HOLDS];
static std::mutex g_mx;

static std::atomic_bool g_stop{false};

// ---- Winsock helpers
static void die(const char* msg){ std::cerr<<"Fatal: "<<msg<<"\n"; std::exit(1); }
static void init_sock(){
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa)!=0) die("WSAStartup failed");
}
static SOCKET listen_on(uint16_t port){
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s==INVALID_SOCKET) die("socket failed");
    int yes=1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=htonl(INADDR_ANY);
    if (bind(s,(sockaddr*)&a,sizeof(a))==SOCKET_ERROR) die("bind failed");
    if (listen(s, 1)==SOCKET_ERROR) die("listen failed");
    return s;
}
static int recv_all(SOCKET c, char* buf, int len){
    int total=0;
    while(total<len){
        int r = ::recv(c, buf+total, len-total, 0);
        if (r<=0) return r;
        total += r;
    }
    return total;
}
static inline void put_u16(std::vector<uint8_t>& buf, uint16_t v){
    buf.push_back((v>>8)&0xFF); buf.push_back(v&0xFF);
}
static inline uint16_t get_u16(const uint8_t* p){ return (uint16_t)p[0]<<8 | (uint16_t)p[1]; }

// ---- Time helpers
static std::pair<std::tm,std::time_t> now_tm(){
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return {lt, t};
}
static inline int minutes_of_day(const std::tm& lt){ return lt.tm_hour*60 + lt.tm_min; }

// ---- Console control handler (graceful shutdown)
#ifdef _WIN32
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT ||
        ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_SHUTDOWN_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}
#endif

// ---- Scheduler core (runs inside emulator)
static void scheduler_loop(){
    // Initialize constant fields
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_holds[HR_NUM_SLOTS] = static_cast<uint16_t>(MAX_SLOTS);
    }

    uint16_t hb = 0;
    while(!g_stop){
        auto [lt, now] = now_tm();
        int tod = minutes_of_day(lt);

        {
            std::lock_guard<std::mutex> lk(g_mx);

            // heartbeat
            g_holds[HR_HEARTBEAT] = hb++;

            for (int i=0;i<MAX_SLOTS;i++){
                uint16_t base = static_cast<uint16_t>(HR_SCHED_BASE + i*HR_SCHED_STRIDE);

                bool     enabled = (g_holds[base + F_ENABLED] != 0);
                uint16_t type    = g_holds[base + F_TYPE];
                uint16_t area    = g_holds[base + F_AREA];
                uint16_t days    = g_holds[base + F_DAYS];
                uint16_t start   = g_holds[base + F_START];
                uint16_t dur     = g_holds[base + F_DUR];

                // remote enable coil
                bool rem_ok = false;
                if (static_cast<int>(COIL_REMOTE_EN_BASE) + i < MAX_COILS)
                    rem_ok = (g_coils[COIL_REMOTE_EN_BASE + i] != 0);
                bool effective = enabled && (rem_ok || (static_cast<int>(COIL_REMOTE_EN_BASE)+i >= MAX_COILS));

                // status interpretation
                uint16_t& status = g_holds[base + F_STATUS];
                if (status > 2) status = 0;

                auto ensure_off = [&](){
                    if (status==1 && area<MAX_COILS) g_coils[area]=0;
                };

                if (!effective){
                    ensure_off();
                    status = (type==1 && status==2) ? 2 : 0; // keep "consumed" for once
                    continue;
                }

                if (dur==0) { // invalid duration
                    ensure_off();
                    status = (type==1 && status==2) ? 2 : 0;
                    continue;
                }

                if (type==0){ // weekly
                    int wday = lt.tm_wday; // 0=Sun..6=Sat
                    bool day_ok = (days & (1u<<wday)) != 0;
                    if (!day_ok){
                        ensure_off();
                        status = 0;
                    } else {
                        int end_min = static_cast<int>(start) + static_cast<int>(dur);
                        if (static_cast<int>(start) <= tod && tod < end_min){
                            if (area<MAX_COILS) g_coils[area]=1;
                            status = 1;
                        } else {
                            ensure_off();
                            status = 0;
                        }
                    }
                } else { // once
                    uint16_t Y = g_holds[base + F_YEAR];
                    uint16_t M = g_holds[base + F_MONTH];
                    uint16_t D = g_holds[base + F_DAY];

                    std::tm stm{};
                    stm.tm_year = static_cast<int>(Y) - 1900;
                    stm.tm_mon  = static_cast<int>(M) - 1;
                    stm.tm_mday = static_cast<int>(D);
                    stm.tm_hour = static_cast<int>(start/60);
                    stm.tm_min  = static_cast<int>(start%60);
                    stm.tm_sec  = 0;
                    stm.tm_isdst = -1;
                    std::time_t t_start = std::mktime(&stm);
                    if (t_start == static_cast<std::time_t>(-1)){
                        ensure_off();
                        status = 0;
                        continue;
                    }
                    std::time_t t_end = t_start + static_cast<std::time_t>(dur)*60;

                    if (now >= t_start && now < t_end){
                        if (area<MAX_COILS) g_coils[area]=1;
                        status = 1;
                    } else if (now >= t_end){
                        ensure_off();
                        status = 2; // consumed
                    } else {
                        ensure_off();
                        if (status!=2) status = 0;
                    }
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ---- Modbus server
int main(int argc, char** argv){
    uint16_t port = 1502;
    uint8_t  unit = 1;
    if (argc>=2) port = static_cast<uint16_t>(std::stoi(argv[1]));
    if (argc>=3) unit = static_cast<uint8_t>(std::stoi(argv[2]));

    std::memset(g_coils, 0, sizeof(g_coils));
    std::memset(g_holds, 0, sizeof(g_holds));

#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT,  [](int){ g_stop = true; });
    std::signal(SIGTERM, [](int){ g_stop = true; });
#endif

    // expose number of slots
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_holds[HR_NUM_SLOTS] = static_cast<uint16_t>(MAX_SLOTS);
    }

    // start scheduler thread
    std::thread sched(scheduler_loop);

    init_sock();
    SOCKET srv = listen_on(port);
    std::cout<<"PLC Scheduler emulator listening on 0.0.0.0:"<<port<<" unit="<<(int)unit<<"\n";

    while(!g_stop){
        sockaddr_in ca{}; int calen=sizeof(ca);
        SOCKET c = ::accept(srv, (sockaddr*)&ca, &calen);
        if (c==INVALID_SOCKET){ if (g_stop) break; continue; }
        char ipstr[64]; inet_ntop(AF_INET, &ca.sin_addr, ipstr, sizeof(ipstr));
        std::cout<<"Client connected: "<<ipstr<<"\n";

        for(;;){
            // MBAP
            uint8_t mbap[7];
            int r = recv_all(c, (char*)mbap, 7);
            if (r<=0){ std::cout<<"Client disconnected\n"; break; }

            uint16_t tx  = get_u16(mbap+0);
            uint16_t pid = get_u16(mbap+2); (void)pid;
            uint16_t len = get_u16(mbap+4);
            uint8_t  uid = mbap[6];
            if (len<1){ std::cout<<"Bad len\n"; break; }

            std::vector<uint8_t> pdu(len);
            r = recv_all(c, (char*)pdu.data(), len);
            if (r<=0){ std::cout<<"Client disconnected\n"; break; }

            uint8_t fc = pdu[0];

            auto send_resp = [&](const std::vector<uint8_t>& rpdu){
                std::vector<uint8_t> resp;
                put_u16(resp, tx);
                put_u16(resp, 0x0000);
                put_u16(resp, static_cast<uint16_t>(rpdu.size()));
                resp.push_back(uid);
                resp.insert(resp.end(), rpdu.begin(), rpdu.end());
                ::send(c, (const char*)resp.data(), (int)resp.size(), 0);
            };
            auto send_ex = [&](uint8_t base_fc, uint8_t code){
                std::vector<uint8_t> rpdu;
                rpdu.push_back(static_cast<uint8_t>(base_fc | 0x80));
                rpdu.push_back(code);
                send_resp(rpdu);
            };

            if (uid != unit){ send_ex(fc, 0x0B); continue; }

            try{
                std::lock_guard<std::mutex> lk(g_mx);

                if (fc==0x05){ // Write Single Coil
                    if (pdu.size()<5){ send_ex(fc,0x03); continue; }
                    uint16_t addr = get_u16(&pdu[1]);
                    uint16_t val  = get_u16(&pdu[3]);
                    if (addr>=MAX_COILS){ send_ex(fc,0x02); continue; }
                    g_coils[addr] = (val==0xFF00)?1:0;
                    // Note: scheduler also writes area coils; SCADA typically writes remote_enable coils here.
                    std::vector<uint8_t> rpdu{0x05};
                    put_u16(rpdu, addr); put_u16(rpdu, (g_coils[addr]?0xFF00:0x0000));
                    send_resp(rpdu);
                }
                else if (fc==0x01){ // Read Coils
                    if (pdu.size()<5){ send_ex(fc,0x03); continue; }
                    uint16_t addr = get_u16(&pdu[1]);
                    uint16_t cnt  = get_u16(&pdu[3]);
                    if ((int)addr + (int)cnt > MAX_COILS){ send_ex(fc,0x02); continue; }
                    uint8_t bc = static_cast<uint8_t>((cnt+7)/8);
                    std::vector<uint8_t> rpdu; rpdu.reserve(2+bc);
                    rpdu.push_back(0x01); rpdu.push_back(bc);
                    uint8_t cur=0;
                    for (uint16_t i=0;i<cnt;i++){
                        uint8_t bit = g_coils[addr+i]?1:0;
                        cur |= (bit << (i%8));
                        if ((i%8)==7){ rpdu.push_back(cur); cur=0; }
                    }
                    if ((cnt%8)!=0) rpdu.push_back(cur);
                    send_resp(rpdu);
                }
                else if (fc==0x06){ // Write Single Holding Register
                    if (pdu.size()<5){ send_ex(fc,0x03); continue; }
                    uint16_t addr = get_u16(&pdu[1]);
                    uint16_t val  = get_u16(&pdu[3]);
                    if (addr>=MAX_HOLDS){ send_ex(fc,0x02); continue; }

                    // protect read-only fields
                    bool ro = (addr==HR_NUM_SLOTS);
                    if (!ro) g_holds[addr] = val;

                    std::vector<uint8_t> rpdu{0x06};
                    put_u16(rpdu, addr); put_u16(rpdu, g_holds[addr]);
                    send_resp(rpdu);
                }
                else if (fc==0x03){ // Read Holding Registers
                    if (pdu.size()<5){ send_ex(fc,0x03); continue; }
                    uint16_t addr = get_u16(&pdu[1]);
                    uint16_t cnt  = get_u16(&pdu[3]);
                    if ((int)addr + (int)cnt > MAX_HOLDS){ send_ex(fc,0x02); continue; }
                    std::vector<uint8_t> rpdu; rpdu.reserve(2+cnt*2);
                    rpdu.push_back(0x03); rpdu.push_back(static_cast<uint8_t>(cnt*2));
                    for (uint16_t i=0;i<cnt;i++) put_u16(rpdu, g_holds[addr+i]);
                    send_resp(rpdu);
                }
                else {
                    send_ex(fc, 0x01); // Illegal Function
                }
            } catch(...){
                send_ex(fc, 0x04); // Slave Device Failure
            }
        }

        ::closesocket(c);
    }

    closesocket(srv);
    g_stop = true;
    if (sched.joinable()) sched.join();
    WSACleanup();
    std::cout<<"PLC Scheduler emulator stopped.\n";
    return 0;
}
