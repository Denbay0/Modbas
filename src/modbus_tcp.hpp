#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Минимальный Modbus/TCP клиент (FC1, FC5, FC6, FC3) на Winsock.
class ModbusTcpClient {
public:
    ModbusTcpClient();
    ~ModbusTcpClient();

    void connect_to(const std::string& ip, uint16_t port, uint8_t unit_id);
    void close();
    bool is_ok() const;

    bool write_coil(uint16_t addr, bool on);                  // FC5
    std::optional<bool> read_coil(uint16_t addr);             // FC1 (1 бит)
    bool write_holding(uint16_t addr, uint16_t val);          // FC6
    std::vector<uint16_t> read_holding(uint16_t addr, uint16_t count); // FC3

private:
    void put_u16(std::vector<uint8_t>& buf, uint16_t v);
    std::vector<uint8_t> xfer(const std::vector<uint8_t>& pdu);
    int recv_all(char* buf, int len);

    void* socket_ = nullptr; // хранит SOCKET, избегая зависимостей в заголовке
    std::string ip_;
    uint16_t port_{502};
    uint8_t  unit_id_{1};
    uint16_t txid_{0};
};
