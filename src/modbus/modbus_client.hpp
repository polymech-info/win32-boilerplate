#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace media::modbus {

enum class Transport {
    Tcp,
    Rtu,
};

struct Endpoint {
    Transport transport = Transport::Tcp;
    std::string host = "127.0.0.1";
    int port = 502;
    std::string device;
    int baud = 9600;
    char parity = 'N';
    int data_bits = 8;
    int stop_bits = 1;
    int slave = 1;
};

struct ServerOptions {
    Endpoint endpoint;
    int unit_id = 1;
    int holding_register_count = 128;
    int coil_count = 128;
    std::vector<std::uint16_t> holding_registers;
    std::vector<std::uint8_t> coils;
    int duration_ms = 0;      // 0 = run until cancellation / process signal
    int max_requests = 0;     // 0 = unlimited
    int poll_timeout_ms = 100;
    bool debug = false;
};

struct ServerStats {
    bool ok = false;
    std::string error;
    std::string url;
    int listen_socket = -1;
    int requests = 0;
    int replies = 0;
    int connections = 0;
    int elapsed_ms = 0;
};

bool parse_endpoint(const std::string& url, Endpoint& out, std::string& err);
std::string normalize_url(const Endpoint& endpoint);

bool read_holding_registers(const std::string& url,
                            int address,
                            int count,
                            int slave,
                            int timeout_ms,
                            std::vector<std::uint16_t>& out,
                            std::string& err);

bool write_register(const std::string& url,
                    int address,
                    std::uint16_t value,
                    int slave,
                    int timeout_ms,
                    std::string& err);

bool write_registers(const std::string& url,
                     int address,
                     const std::vector<std::uint16_t>& values,
                     int slave,
                     int timeout_ms,
                     std::string& err);

bool run_tcp_server(const ServerOptions& options,
                    const std::function<bool()>& cancel_requested,
                    ServerStats& stats);

} // namespace media::modbus
