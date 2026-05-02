#include "discovery.h"

#include "text_encoding.h"

#include <chrono>
#include <sstream>
#include <string_view>
#include <ws2tcpip.h>

namespace lightnote {
namespace {

constexpr int kAnnounceIntervalMs = 1000;
constexpr char kDiscoveryPrefix[] = "SHARENOTEPAD_DISCOVERY";

bool SetBroadcast(SOCKET socket) {
    BOOL enabled = TRUE;
    return setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
}

bool SetReuseAddress(SOCKET socket) {
    BOOL enabled = TRUE;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled)) == 0;
}

bool SetReceiveTimeout(SOCKET socket, DWORD timeout_ms) {
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) == 0;
}

sockaddr_in MakeTarget(const char* address, uint16_t port) {
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    InetPtonA(AF_INET, address, &target.sin_addr);
    return target;
}

std::wstring AsWideAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

bool ReadTokenValue(std::string_view token, std::string_view key, std::string* value) {
    const std::string prefix = std::string(key) + "=";
    if (token.rfind(prefix, 0) != 0) {
        return false;
    }

    *value = std::string(token.substr(prefix.size()));
    return true;
}

}  // namespace

DiscoveryAnnouncer::DiscoveryAnnouncer() = default;

DiscoveryAnnouncer::~DiscoveryAnnouncer() {
    Stop();
}

bool DiscoveryAnnouncer::Start(const std::wstring& session_code, uint16_t tcp_port, uint16_t discovery_port) {
    Stop();

    session_code_ = session_code;
    tcp_port_ = tcp_port;
    discovery_port_ = discovery_port;

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        last_error_ = L"discovery socket creation failed";
        return false;
    }

    if (!SetBroadcast(socket_)) {
        last_error_ = L"discovery broadcast option failed";
        CloseSocket();
        return false;
    }

    running_ = true;
    thread_ = std::thread(&DiscoveryAnnouncer::AnnounceLoop, this);
    return true;
}

void DiscoveryAnnouncer::Stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    CloseSocket();
}

bool DiscoveryAnnouncer::running() const {
    return running_.load();
}

const std::wstring& DiscoveryAnnouncer::last_error() const {
    return last_error_;
}

void DiscoveryAnnouncer::AnnounceLoop() {
    const sockaddr_in broadcast = MakeTarget("255.255.255.255", discovery_port_);
    const sockaddr_in loopback = MakeTarget("127.0.0.1", discovery_port_);

    while (running_) {
        SendAnnouncement(broadcast);
        SendAnnouncement(loopback);
        std::this_thread::sleep_for(std::chrono::milliseconds(kAnnounceIntervalMs));
    }
}

bool DiscoveryAnnouncer::SendAnnouncement(const sockaddr_in& target) {
    const std::string message = BuildMessage();
    const int sent = sendto(
        socket_,
        message.data(),
        static_cast<int>(message.size()),
        0,
        reinterpret_cast<const sockaddr*>(&target),
        sizeof(target));

    if (sent == SOCKET_ERROR) {
        last_error_ = L"discovery send failed";
        return false;
    }

    return true;
}

std::string DiscoveryAnnouncer::BuildMessage() const {
    std::ostringstream builder;
    builder << kDiscoveryPrefix
            << " v=1 session=" << WideToUtf8(session_code_)
            << " port=" << tcp_port_;
    return builder.str();
}

void DiscoveryAnnouncer::CloseSocket() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool DiscoveryClient::Find(
    const std::wstring& session_code,
    int timeout_ms,
    DiscoveryEndpoint* endpoint,
    uint16_t discovery_port) {
    endpoint->host.clear();
    endpoint->port = 0;
    last_error_.clear();

    SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        last_error_ = L"discovery socket creation failed";
        return false;
    }

    SetReuseAddress(socket_handle);
    SetReceiveTimeout(socket_handle, 100);

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(discovery_port);

    if (bind(socket_handle, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        closesocket(socket_handle);
        last_error_ = L"discovery bind failed";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    char buffer[512]{};

    while (std::chrono::steady_clock::now() < deadline) {
        sockaddr_in sender{};
        int sender_size = sizeof(sender);
        const int received = recvfrom(
            socket_handle,
            buffer,
            static_cast<int>(sizeof(buffer) - 1),
            0,
            reinterpret_cast<sockaddr*>(&sender),
            &sender_size);

        if (received == SOCKET_ERROR) {
            continue;
        }

        std::string message(buffer, buffer + received);
        uint16_t tcp_port = 0;
        if (!ParseMessage(message, session_code, &tcp_port)) {
            continue;
        }

        char host[INET_ADDRSTRLEN]{};
        if (InetNtopA(AF_INET, &sender.sin_addr, host, INET_ADDRSTRLEN) == nullptr) {
            continue;
        }

        endpoint->host = AsWideAscii(host);
        endpoint->port = tcp_port;
        closesocket(socket_handle);
        return true;
    }

    closesocket(socket_handle);
    last_error_ = L"discovery timeout";
    return false;
}

const std::wstring& DiscoveryClient::last_error() const {
    return last_error_;
}

bool DiscoveryClient::ParseMessage(const std::string& message, const std::wstring& expected_session, uint16_t* tcp_port) {
    std::istringstream stream(message);
    std::string prefix;
    stream >> prefix;
    if (prefix != kDiscoveryPrefix) {
        return false;
    }

    std::string token;
    std::string session;
    uint16_t port = 0;

    while (stream >> token) {
        std::string value;
        if (ReadTokenValue(token, "session", &value)) {
            session = value;
            continue;
        }

        if (ReadTokenValue(token, "port", &value)) {
            try {
                const unsigned long parsed = std::stoul(value);
                if (parsed == 0 || parsed > 65535) {
                    return false;
                }
                port = static_cast<uint16_t>(parsed);
            } catch (...) {
                return false;
            }
        }
    }

    std::wstring expected_utf16;
    if (!Utf8ToWide(session, &expected_utf16)) {
        return false;
    }

    if (expected_utf16 != expected_session || port == 0) {
        return false;
    }

    *tcp_port = port;
    return true;
}

}  // namespace lightnote
