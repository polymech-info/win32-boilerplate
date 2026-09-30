#include "modbus/modbus_client.hpp"

#include <modbus.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace media::modbus {
namespace {

std::string lower_ascii(std::string s)
{
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::vector<std::string> split(const std::string& text, char delim)
{
    std::vector<std::string> out;
    std::string item;
    std::istringstream stream(text);
    while (std::getline(stream, item, delim))
        out.push_back(item);
    return out;
}

bool parse_int(const std::string& text, int& out)
{
    if (text.empty())
        return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0')
        return false;
    out = static_cast<int>(value);
    return true;
}

std::string libmodbus_error(const char* what)
{
    std::string msg = what ? what : "modbus";
    msg += ": ";
    msg += modbus_strerror(errno);
    return msg;
}

void set_timeout(modbus_t* ctx, int timeout_ms)
{
    if (!ctx || timeout_ms <= 0)
        return;
    const std::uint32_t sec = static_cast<std::uint32_t>(timeout_ms / 1000);
    const std::uint32_t usec = static_cast<std::uint32_t>((timeout_ms % 1000) * 1000);
    modbus_set_response_timeout(ctx, sec, usec);
    modbus_set_byte_timeout(ctx, sec, usec);
}

struct ModbusDeleter {
    void operator()(modbus_t* ctx) const
    {
        if (!ctx)
            return;
        modbus_close(ctx);
        modbus_free(ctx);
    }
};

using ModbusPtr = std::unique_ptr<modbus_t, ModbusDeleter>;

ModbusPtr make_context(const Endpoint& endpoint, std::string& err)
{
    modbus_t* raw = nullptr;
    if (endpoint.transport == Transport::Tcp) {
        raw = modbus_new_tcp(endpoint.host.c_str(), endpoint.port);
    } else {
        raw = modbus_new_rtu(endpoint.device.c_str(),
                             endpoint.baud,
                             endpoint.parity,
                             endpoint.data_bits,
                             endpoint.stop_bits);
    }
    if (!raw) {
        err = libmodbus_error("modbus_new");
        return nullptr;
    }
    if (endpoint.slave > 0)
        modbus_set_slave(raw, endpoint.slave);
    return ModbusPtr(raw);
}

bool connect_context(modbus_t* ctx, int timeout_ms, std::string& err)
{
    set_timeout(ctx, timeout_ms);
    if (modbus_connect(ctx) == -1) {
        err = libmodbus_error("modbus_connect");
        return false;
    }
    return true;
}

bool transient_receive_errno(int err)
{
#if defined(_WIN32)
    return err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEINTR;
#else
    return err == ETIMEDOUT || err == EAGAIN || err == EINTR;
#endif
}

void close_socket_int(int socket_fd)
{
    if (socket_fd < 0)
        return;
#if defined(_WIN32)
    closesocket(static_cast<SOCKET>(socket_fd));
#else
    close(socket_fd);
#endif
}

bool duration_elapsed(std::chrono::steady_clock::time_point start, int duration_ms)
{
    if (duration_ms <= 0)
        return false;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return elapsed.count() >= duration_ms;
}

} // namespace

bool parse_endpoint(const std::string& url, Endpoint& out, std::string& err)
{
    std::string text = url;
    const auto scheme_pos = text.find("://");
    if (scheme_pos != std::string::npos)
        text = text.substr(0, scheme_pos) + ":" + text.substr(scheme_pos + 3);

    const auto parts = split(text, ':');
    if (parts.empty() || parts[0].empty()) {
        err = "modbus URL is empty; expected tcp:host:port or rtu:device:baud[:8N1]";
        return false;
    }

    const std::string kind = lower_ascii(parts[0]);
    if (kind == "tcp") {
        if (parts.size() < 2 || parts[1].empty()) {
            err = "tcp URL requires host: tcp:127.0.0.1:1502";
            return false;
        }
        out = Endpoint{};
        out.transport = Transport::Tcp;
        out.host = parts[1];
        if (parts.size() >= 3 && !parts[2].empty() && !parse_int(parts[2], out.port)) {
            err = "tcp URL port is not an integer";
            return false;
        }
        if (parts.size() >= 4 && !parts[3].empty() && !parse_int(parts[3], out.slave)) {
            err = "tcp URL slave/unit id is not an integer";
            return false;
        }
        return true;
    }

    if (kind == "rtu") {
        if (parts.size() < 2 || parts[1].empty()) {
            err = "rtu URL requires device: rtu:COM3:9600:8N1";
            return false;
        }
        out = Endpoint{};
        out.transport = Transport::Rtu;
        out.device = parts[1];
        if (parts.size() >= 3 && !parts[2].empty() && !parse_int(parts[2], out.baud)) {
            err = "rtu URL baud is not an integer";
            return false;
        }
        if (parts.size() >= 4 && !parts[3].empty()) {
            const std::string framing = parts[3];
            if (framing.size() >= 3) {
                out.data_bits = framing[0] - '0';
                out.parity = static_cast<char>(std::toupper(static_cast<unsigned char>(framing[1])));
                out.stop_bits = framing[2] - '0';
            }
        }
        if (parts.size() >= 5 && !parts[4].empty() && !parse_int(parts[4], out.slave)) {
            err = "rtu URL slave id is not an integer";
            return false;
        }
        return true;
    }

    err = "unsupported modbus URL scheme '" + parts[0] + "'; expected tcp or rtu";
    return false;
}

std::string normalize_url(const Endpoint& endpoint)
{
    std::ostringstream out;
    if (endpoint.transport == Transport::Tcp) {
        out << "tcp:" << endpoint.host << ":" << endpoint.port;
    } else {
        out << "rtu:" << endpoint.device << ":" << endpoint.baud << ":"
            << endpoint.data_bits << endpoint.parity << endpoint.stop_bits;
    }
    return out.str();
}

bool read_holding_registers(const std::string& url,
                            int address,
                            int count,
                            int slave,
                            int timeout_ms,
                            std::vector<std::uint16_t>& out,
                            std::string& err)
{
    Endpoint endpoint;
    if (!parse_endpoint(url, endpoint, err))
        return false;
    if (slave > 0)
        endpoint.slave = slave;
    auto ctx = make_context(endpoint, err);
    if (!ctx)
        return false;
    if (!connect_context(ctx.get(), timeout_ms, err))
        return false;
    out.assign(static_cast<std::size_t>(std::max(0, count)), 0);
    const int rc = modbus_read_registers(ctx.get(), address, count, out.data());
    if (rc == -1) {
        err = libmodbus_error("modbus_read_registers");
        return false;
    }
    out.resize(static_cast<std::size_t>(rc));
    return true;
}

bool write_register(const std::string& url,
                    int address,
                    std::uint16_t value,
                    int slave,
                    int timeout_ms,
                    std::string& err)
{
    Endpoint endpoint;
    if (!parse_endpoint(url, endpoint, err))
        return false;
    if (slave > 0)
        endpoint.slave = slave;
    auto ctx = make_context(endpoint, err);
    if (!ctx)
        return false;
    if (!connect_context(ctx.get(), timeout_ms, err))
        return false;
    if (modbus_write_register(ctx.get(), address, value) == -1) {
        err = libmodbus_error("modbus_write_register");
        return false;
    }
    return true;
}

bool write_registers(const std::string& url,
                     int address,
                     const std::vector<std::uint16_t>& values,
                     int slave,
                     int timeout_ms,
                     std::string& err)
{
    Endpoint endpoint;
    if (!parse_endpoint(url, endpoint, err))
        return false;
    if (slave > 0)
        endpoint.slave = slave;
    auto ctx = make_context(endpoint, err);
    if (!ctx)
        return false;
    if (!connect_context(ctx.get(), timeout_ms, err))
        return false;
    if (values.empty())
        return true;
    const int rc = modbus_write_registers(ctx.get(),
                                          address,
                                          static_cast<int>(values.size()),
                                          values.data());
    if (rc == -1) {
        err = libmodbus_error("modbus_write_registers");
        return false;
    }
    return true;
}

bool run_tcp_server(const ServerOptions& options,
                    const std::function<bool()>& cancel_requested,
                    ServerStats& stats)
{
    stats = {};
    stats.url = normalize_url(options.endpoint);
    if (options.endpoint.transport != Transport::Tcp) {
        stats.error = "server currently supports tcp URLs only";
        return false;
    }

    std::string err;
    auto ctx = make_context(options.endpoint, err);
    if (!ctx) {
        stats.error = err;
        return false;
    }
    if (options.debug)
        modbus_set_debug(ctx.get(), TRUE);
    modbus_set_slave(ctx.get(), options.unit_id > 0 ? options.unit_id : options.endpoint.slave);
    const int poll_ms = std::max(1, options.poll_timeout_ms);
    modbus_set_indication_timeout(ctx.get(),
                                  static_cast<std::uint32_t>(poll_ms / 1000),
                                  static_cast<std::uint32_t>((poll_ms % 1000) * 1000));

    const int server_socket = modbus_tcp_listen(ctx.get(), 1);
    if (server_socket == -1) {
        stats.error = libmodbus_error("modbus_tcp_listen");
        return false;
    }
    stats.listen_socket = server_socket;

    const int nb_registers = std::max(1, options.holding_register_count);
    const int nb_bits = std::max(1, options.coil_count);
    modbus_mapping_t* raw_mapping = modbus_mapping_new(nb_bits, nb_bits, nb_registers, nb_registers);
    std::unique_ptr<modbus_mapping_t, decltype(&modbus_mapping_free)> mapping(raw_mapping, modbus_mapping_free);
    if (!mapping) {
        stats.error = libmodbus_error("modbus_mapping_new");
        close_socket_int(server_socket);
        return false;
    }
    for (std::size_t i = 0; i < options.holding_registers.size() && i < static_cast<std::size_t>(nb_registers); ++i)
        mapping->tab_registers[i] = options.holding_registers[i];
    for (std::size_t i = 0; i < options.coils.size() && i < static_cast<std::size_t>(nb_bits); ++i)
        mapping->tab_bits[i] = options.coils[i] ? TRUE : FALSE;

    const auto start = std::chrono::steady_clock::now();
    std::uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH] = {};
    bool ok = true;
    while (!(cancel_requested && cancel_requested()) &&
           !duration_elapsed(start, options.duration_ms) &&
           (options.max_requests <= 0 || stats.requests < options.max_requests)) {
        int accept_socket = server_socket;
        if (modbus_tcp_accept(ctx.get(), &accept_socket) == -1) {
            stats.error = libmodbus_error("modbus_tcp_accept");
            ok = false;
            break;
        }
        ++stats.connections;

        while (!(cancel_requested && cancel_requested()) &&
               !duration_elapsed(start, options.duration_ms) &&
               (options.max_requests <= 0 || stats.requests < options.max_requests)) {
            const int rc = modbus_receive(ctx.get(), query);
            if (rc > 0) {
                ++stats.requests;
                if (modbus_reply(ctx.get(), query, rc, mapping.get()) == -1) {
                    stats.error = libmodbus_error("modbus_reply");
                    ok = false;
                    break;
                }
                ++stats.replies;
                continue;
            }
            if (rc == -1 && transient_receive_errno(errno))
                continue;
            break; // client closed or non-timeout receive error; go back to accept
        }
        modbus_close(ctx.get());
        if (!ok)
            break;
    }

    close_socket_int(server_socket);
    stats.elapsed_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count());
    stats.ok = ok;
    return ok;
}

} // namespace media::modbus
