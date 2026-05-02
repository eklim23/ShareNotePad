#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include <winsock2.h>

namespace lightnote {

constexpr uint16_t kDiscoveryPort = 7776;

struct DiscoveryEndpoint {
    std::wstring host;
    uint16_t port = 0;
};

class DiscoveryAnnouncer {
public:
    DiscoveryAnnouncer();
    ~DiscoveryAnnouncer();

    DiscoveryAnnouncer(const DiscoveryAnnouncer&) = delete;
    DiscoveryAnnouncer& operator=(const DiscoveryAnnouncer&) = delete;

    bool Start(const std::wstring& session_code, uint16_t tcp_port, uint16_t discovery_port = kDiscoveryPort);
    void Stop();

    bool running() const;
    const std::wstring& last_error() const;

private:
    void AnnounceLoop();
    bool SendAnnouncement(const sockaddr_in& target);
    std::string BuildMessage() const;
    void CloseSocket();

    SOCKET socket_ = INVALID_SOCKET;
    std::wstring session_code_;
    uint16_t tcp_port_ = 0;
    uint16_t discovery_port_ = kDiscoveryPort;
    std::thread thread_;
    std::atomic_bool running_ = false;
    std::wstring last_error_;
};

class DiscoveryClient {
public:
    bool Find(
        const std::wstring& session_code,
        int timeout_ms,
        DiscoveryEndpoint* endpoint,
        uint16_t discovery_port = kDiscoveryPort);

    const std::wstring& last_error() const;

private:
    bool ParseMessage(const std::string& message, const std::wstring& expected_session, uint16_t* tcp_port);

    std::wstring last_error_;
};

}  // namespace lightnote

