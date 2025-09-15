#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "modbus_tcp.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <stdexcept>
#include <string>

ModbusTcpClient::ModbusTcpClient() {}
ModbusTcpClient::~ModbusTcpClient(){ close(); WSACleanup(); }

void ModbusTcpClient::connect_to(const std::string& ip, uint16_t port, uint8_t unit_id) {
    ip_ = ip; port_ = port; unit_id_ = unit_id;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0)
        throw std::runtime_error("WSAStartup failed");

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) throw std::runtime_error("socket() failed");
    socket_ = reinterpret_cast<void*>(s);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1)
        throw std::runtime_error("inet_pton failed");

    DWORD tv = 1000; // 1s
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(s); socket_ = nullptr;
        throw std::runtime_error("connect() failed");
    }
}

void ModbusTcpClient::close() {
    if (socket_) {
        SOCKET s = reinterpret_cast<SOCKET>(socket_);
        ::closesocket(s);
        socket_ = nullptr;
    }
}

bool ModbusTcpClient::is_ok() const { return socket_ != nullptr; }

bool ModbusTcpClient::write_coil(uint16_t addr, bool on) {
    std::vector<uint8_t> pdu{0x05};
    put_u16(pdu, addr);
    put_u16(pdu, on ? 0xFF00 : 0x0000);
    auto resp = xfer(pdu);
    return resp.size()==5 && resp[0]==0x05;
}

std::optional<bool> ModbusTcpClient::read_coil(uint16_t addr) {
    std::vector<uint8_t> pdu{0x01};
    put_u16(pdu, addr);
    put_u16(pdu, 1);
    auto resp = xfer(pdu);
    if (resp.size()>=3 && resp[0]==0x01 && resp[1]==0x01)
        return (resp[2] & 0x01) ? true : false;
    return std::nullopt;
}

bool ModbusTcpClient::write_holding(uint16_t addr, uint16_t val) {
    std::vector<uint8_t> pdu{0x06};
    put_u16(pdu, addr);
    put_u16(pdu, val);
    auto resp = xfer(pdu);
    return resp.size()==5 && resp[0]==0x06;
}

std::vector<uint16_t> ModbusTcpClient::read_holding(uint16_t addr, uint16_t count) {
    std::vector<uint8_t> pdu{0x03};
    put_u16(pdu, addr);
    put_u16(pdu, count);
    auto resp = xfer(pdu);
    std::vector<uint16_t> out;
    if (resp.size()>=2 && resp[0]==0x03) {
        uint8_t bc = resp[1];
        if (bc == count*2 && resp.size() == (size_t)(2+bc)) {
            for (uint16_t i=0;i<count;++i) {
                uint16_t v = ((uint16_t)resp[2+i*2]<<8) | (uint16_t)resp[3+i*2];
                out.push_back(v);
            }
        }
    }
    return out;
}

void ModbusTcpClient::put_u16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(uint8_t((v>>8) & 0xFF));
    buf.push_back(uint8_t(v & 0xFF));
}

std::vector<uint8_t> ModbusTcpClient::xfer(const std::vector<uint8_t>& pdu) {
    if (!is_ok()) throw std::runtime_error("socket closed");
    SOCKET s = reinterpret_cast<SOCKET>(socket_);

    std::vector<uint8_t> req;
    uint16_t tx = ++txid_;
    put_u16(req, tx);
    put_u16(req, 0x0000);
    uint16_t len = (uint16_t)(pdu.size()+1);
    put_u16(req, len);
    req.push_back(unit_id_);
    req.insert(req.end(), pdu.begin(), pdu.end());

    int sent = ::send(s, (const char*)req.data(), (int)req.size(), 0);
    if (sent != (int)req.size()) throw std::runtime_error("send failed");

    uint8_t mbap[7];
    int got = recv_all((char*)mbap, 7);
    if (got != 7) throw std::runtime_error("recv mbap failed");

    uint16_t rx_len = (mbap[4]<<8) | mbap[5];
    uint8_t  rx_uid = mbap[6];
    if (rx_uid != unit_id_) throw std::runtime_error("unit id mismatch");
    if (rx_len < 1) throw std::runtime_error("bad length");

    std::vector<uint8_t> resp(rx_len);
    int got2 = recv_all((char*)resp.data(), rx_len);
    if (got2 != rx_len) throw std::runtime_error("recv pdu failed");

    if (!resp.empty() && (resp[0] & 0x80)) {
        uint8_t fc = resp[0] & 0x7F;
        uint8_t ex = resp.size()>1?resp[1]:0;
        throw std::runtime_error("Modbus exception fc="+std::to_string(fc)+" code="+std::to_string(ex));
    }
    return resp;
}

int ModbusTcpClient::recv_all(char* buf, int len) {
    SOCKET s = reinterpret_cast<SOCKET>(socket_);
    int total=0;
    while (total<len) {
        int r = ::recv(s, buf+total, len-total, 0);
        if (r == SOCKET_ERROR) return r;
        if (r == 0) break;
        total += r;
    }
    return total;
}
