#include "sync_client.h"

#include "protocol.h"

#include <ws2tcpip.h>

#include <chrono>
#include <thread>
#include <utility>

namespace lightnote {
namespace {

constexpr DWORD kConnectTimeoutMs = 3000;
constexpr DWORD kJoinResponseTimeoutMs = 5000;
constexpr DWORD kSocketIoTimeoutMs = 1000;
constexpr uint64_t kHeartbeatIntervalMs = 5000;
constexpr uint64_t kHeartbeatTimeoutMs = 15000;

enum class ReceiveResult {
    Message,
    Timeout,
    Closed,
    Error,
};

enum class ConnectResult {
    Connected,
    Timeout,
    Failed,
};

uint64_t NowTickMs() {
    return GetTickCount64();
}

bool SendAll(SOCKET socket, const std::string& bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const int result = send(socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
        if (result == SOCKET_ERROR || result == 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }

    return true;
}

bool SendProtocolFrame(SOCKET socket, const std::string& frame) {
    return SendAll(socket, frame);
}

bool SendProtocolMessage(SOCKET socket, const ProtocolMessage& message) {
    std::string json;
    ProtocolError error = ProtocolError::None;
    if (!SerializeMessage(message, &json, &error)) {
        return false;
    }

    return SendAll(socket, EncodeFrame(json));
}

ReceiveResult ReceiveProtocolMessage(SOCKET socket, FrameDecoder* decoder, ProtocolMessage* message) {
    char buffer[4096]{};

    while (true) {
        std::string payload;
        ProtocolError frame_error = ProtocolError::None;
        if (decoder->TryReadFrame(&payload, &frame_error)) {
            ProtocolError message_error = ProtocolError::None;
            return DeserializeMessage(payload, message, &message_error) ? ReceiveResult::Message : ReceiveResult::Error;
        }

        if (frame_error != ProtocolError::None) {
            return ReceiveResult::Error;
        }

        const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received == 0) {
            return ReceiveResult::Closed;
        }

        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                return ReceiveResult::Timeout;
            }
            return ReceiveResult::Error;
        }

        if (!decoder->Append(std::string_view(buffer, static_cast<size_t>(received)), &frame_error)) {
            return ReceiveResult::Error;
        }
    }
}

bool ReceiveProtocolMessageWithin(SOCKET socket, FrameDecoder* decoder, ProtocolMessage* message, DWORD timeout_ms) {
    const uint64_t deadline = NowTickMs() + timeout_ms;
    while (NowTickMs() <= deadline) {
        const ReceiveResult result = ReceiveProtocolMessage(socket, decoder, message);
        if (result == ReceiveResult::Message) {
            return true;
        }

        if (result != ReceiveResult::Timeout) {
            return false;
        }
    }

    return false;
}

void SetSocketTimeouts(SOCKET socket) {
    const DWORD timeout_ms = kSocketIoTimeoutMs;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
}

void SetTcpNoDelay(SOCKET socket) {
    const BOOL enabled = TRUE;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
}

ConnectResult ConnectWithTimeout(SOCKET socket, const sockaddr_in& address, DWORD timeout_ms) {
    u_long non_blocking = 1;
    if (ioctlsocket(socket, FIONBIO, &non_blocking) != 0) {
        return ConnectResult::Failed;
    }

    const int result = connect(
        socket,
        reinterpret_cast<const sockaddr*>(&address),
        static_cast<int>(sizeof(address)));

    if (result == 0) {
        non_blocking = 0;
        ioctlsocket(socket, FIONBIO, &non_blocking);
        return ConnectResult::Connected;
    }

    const int connect_error = WSAGetLastError();
    if (connect_error != WSAEWOULDBLOCK && connect_error != WSAEINPROGRESS && connect_error != WSAEINVAL) {
        non_blocking = 0;
        ioctlsocket(socket, FIONBIO, &non_blocking);
        return ConnectResult::Failed;
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(socket, &write_set);

    fd_set except_set;
    FD_ZERO(&except_set);
    FD_SET(socket, &except_set);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);

    const int selected = select(0, nullptr, &write_set, &except_set, &timeout);
    non_blocking = 0;
    ioctlsocket(socket, FIONBIO, &non_blocking);

    if (selected == 0) {
        return ConnectResult::Timeout;
    }

    if (selected == SOCKET_ERROR || !FD_ISSET(socket, &write_set)) {
        return ConnectResult::Failed;
    }

    int socket_error = 0;
    int socket_error_size = sizeof(socket_error);
    if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &socket_error_size) != 0 ||
        socket_error != 0) {
        return ConnectResult::Failed;
    }

    return ConnectResult::Connected;
}

}  // namespace

SyncClient::SyncClient() = default;

SyncClient::~SyncClient() {
    Disconnect();
}

bool SyncClient::Connect(
    const std::wstring& host,
    uint16_t port,
    const std::wstring& session_code,
    const std::wstring& device_id,
    const std::wstring& nickname) {
    const std::wstring previous_session = session_code_;
    const std::wstring previous_reconnect_token = reconnect_token_;
    Disconnect();
    join_fail_reason_.clear();
    last_error_.clear();
    assigned_client_.clear();
    reconnect_token_ = previous_session == session_code ? previous_reconnect_token : L"";
    session_code_ = session_code;
    pong_count_ = 0;
    const uint64_t now = NowTickMs();
    last_pong_tick_ms_ = now;
    last_ping_tick_ms_ = now;
    stopping_ = false;

    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
        last_error_ = L"socket creation failed";
        return false;
    }

    SetSocketTimeouts(socket_);
    SetTcpNoDelay(socket_);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonW(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        last_error_ = L"invalid host address";
        CloseSocket();
        return false;
    }

    const ConnectResult connect_result = ConnectWithTimeout(socket_, address, kConnectTimeoutMs);
    if (connect_result != ConnectResult::Connected) {
        last_error_ = connect_result == ConnectResult::Timeout ? L"connect timeout" : L"connect failed";
        CloseSocket();
        return false;
    }

    if (!SendHelloAndJoin(session_code, device_id, nickname)) {
        ReadJoinResponse();
        if (!join_fail_reason_.empty()) {
            return false;
        }
        last_error_ = L"join request failed";
        CloseSocket();
        return false;
    }

    if (!ReadJoinResponse()) {
        return false;
    }

    StartWorkers();
    SendSyncRequest(0);
    return true;
}

void SyncClient::Disconnect() {
    connected_ = false;
    stopping_ = true;
    StopWorkers();
    CloseSocket();
}

bool SyncClient::SendPing(uint64_t timestamp) {
    ProtocolMessage ping;
    ping.type = MessageType::Ping;
    ping.session = session_code_.empty() ? L"connected" : session_code_;
    ping.client = assigned_client_.empty() ? L"B" : assigned_client_;
    ping.SetNumber(L"time", timestamp);
    return EnqueueMessage(ping);
}

bool SyncClient::SendInsert(uint64_t base_revision, size_t position, std::wstring text) {
    if (text.empty()) {
        last_error_ = L"empty insert";
        return false;
    }

    ProtocolMessage insert;
    insert.type = MessageType::Insert;
    insert.session = session_code_;
    insert.client = assigned_client_.empty() ? L"B" : assigned_client_;
    insert.SetNumber(L"base_rev", base_revision);
    insert.SetNumber(L"pos", static_cast<uint64_t>(position));
    insert.SetString(L"text", std::move(text));
    return EnqueueMessage(insert);
}

bool SyncClient::SendDelete(uint64_t base_revision, size_t position, size_t length) {
    if (length == 0) {
        last_error_ = L"empty delete";
        return false;
    }

    ProtocolMessage remove;
    remove.type = MessageType::Delete;
    remove.session = session_code_;
    remove.client = assigned_client_.empty() ? L"B" : assigned_client_;
    remove.SetNumber(L"base_rev", base_revision);
    remove.SetNumber(L"pos", static_cast<uint64_t>(position));
    remove.SetNumber(L"len", static_cast<uint64_t>(length));
    return EnqueueMessage(remove);
}

bool SyncClient::SendSyncRequest(uint64_t current_revision) {
    ProtocolMessage request;
    request.type = MessageType::SyncRequest;
    request.session = session_code_;
    request.client = assigned_client_.empty() ? L"B" : assigned_client_;
    request.SetNumber(L"current_rev", current_revision);
    return EnqueueMessage(request);
}

void SyncClient::SetEventCallback(SyncEventCallback callback) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    event_callback_ = std::move(callback);
}

bool SyncClient::TryPopEvent(SyncEvent* event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (event_queue_.empty()) {
        return false;
    }

    *event = std::move(event_queue_.front());
    event_queue_.pop();
    return true;
}

bool SyncClient::connected() const {
    return connected_.load();
}

uint64_t SyncClient::pong_count() const {
    return pong_count_.load();
}

const std::wstring& SyncClient::assigned_client() const {
    return assigned_client_;
}

const std::wstring& SyncClient::reconnect_token() const {
    return reconnect_token_;
}

const std::wstring& SyncClient::join_fail_reason() const {
    return join_fail_reason_;
}

const std::wstring& SyncClient::last_error() const {
    return last_error_;
}

bool SyncClient::SendHelloAndJoin(
    const std::wstring& session_code,
    const std::wstring& device_id,
    const std::wstring& nickname) {
    ProtocolMessage hello;
    hello.type = MessageType::Hello;
    hello.session = session_code;
    hello.client = L"B";
    hello.SetNumber(L"protocol", 1);
    hello.SetString(L"device_id", device_id);
    hello.SetString(L"nickname", nickname);

    ProtocolMessage join;
    join.type = MessageType::Join;
    join.session = session_code;
    join.client = L"B";
    join.SetString(L"join_code", session_code);
    join.SetString(L"reconnect_token", reconnect_token_);

    return SendProtocolMessage(socket_, hello) && SendProtocolMessage(socket_, join);
}

bool SyncClient::ReadJoinResponse() {
    FrameDecoder decoder;
    ProtocolMessage response;
    if (!ReceiveProtocolMessageWithin(socket_, &decoder, &response, kJoinResponseTimeoutMs)) {
        last_error_ = L"join response failed";
        CloseSocket();
        return false;
    }

    if (response.type == MessageType::JoinOk) {
        response.GetString(L"assigned_client", &assigned_client_);
        response.GetString(L"reconnect_token", &reconnect_token_);
        connected_ = true;
        return true;
    }

    if (response.type == MessageType::JoinFail) {
        response.GetString(L"reason", &join_fail_reason_);
        connected_ = false;
        CloseSocket();
        return false;
    }

    last_error_ = L"unexpected join response";
    CloseSocket();
    return false;
}

bool SyncClient::EnqueueMessage(const ProtocolMessage& message) {
    std::string json;
    ProtocolError error = ProtocolError::None;
    if (!SerializeMessage(message, &json, &error)) {
        last_error_ = L"message serialize failed";
        return false;
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    if (!connected_ || stopping_) {
        last_error_ = L"client is not connected";
        return false;
    }

    send_queue_.push(EncodeFrame(json));
    send_cv_.notify_one();
    return true;
}

void SyncClient::StartWorkers() {
    send_thread_ = std::thread(&SyncClient::SendLoop, this);
    receive_thread_ = std::thread(&SyncClient::ReceiveLoop, this);
    heartbeat_thread_ = std::thread(&SyncClient::HeartbeatLoop, this);
}

void SyncClient::StopWorkers() {
    send_cv_.notify_all();

    if (send_thread_.joinable() && send_thread_.get_id() != std::this_thread::get_id()) {
        send_thread_.join();
    }

    if (receive_thread_.joinable() && receive_thread_.get_id() != std::this_thread::get_id()) {
        receive_thread_.join();
    }

    if (heartbeat_thread_.joinable() && heartbeat_thread_.get_id() != std::this_thread::get_id()) {
        heartbeat_thread_.join();
    }

    std::lock_guard<std::mutex> lock(send_mutex_);
    std::queue<std::string> empty;
    std::swap(send_queue_, empty);
}

void SyncClient::SendLoop() {
    while (!stopping_) {
        std::string frame;
        {
            std::unique_lock<std::mutex> lock(send_mutex_);
            send_cv_.wait(lock, [this] {
                return stopping_ || !send_queue_.empty();
            });

            if (stopping_) {
                break;
            }

            frame = std::move(send_queue_.front());
            send_queue_.pop();
        }

        SOCKET socket = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket = socket_;
        }

        if (socket == INVALID_SOCKET || !SendProtocolFrame(socket, frame)) {
            last_error_ = L"send failed";
            connected_ = false;
            CloseSocket();
            break;
        }
    }
}

void SyncClient::ReceiveLoop() {
    FrameDecoder decoder;

    while (!stopping_) {
        SOCKET socket = INVALID_SOCKET;
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket = socket_;
        }

        if (socket == INVALID_SOCKET) {
            connected_ = false;
            break;
        }

        ProtocolMessage message;
        const ReceiveResult result = ReceiveProtocolMessage(socket, &decoder, &message);
        if (result == ReceiveResult::Timeout) {
            continue;
        }

        if (result != ReceiveResult::Message) {
            if (!stopping_) {
                last_error_ = result == ReceiveResult::Closed ? L"connection closed" : L"receive failed";
            }
            connected_ = false;
            CloseSocket();
            send_cv_.notify_all();
            if (!stopping_) {
                SyncEvent event;
                event.type = SyncEventType::Disconnected;
                event.detail = last_error_;
                PushEvent(std::move(event));
            }
            break;
        }

        if (message.type == MessageType::Pong) {
            ++pong_count_;
            last_pong_tick_ms_ = NowTickMs();
        } else if (message.type == MessageType::Apply) {
            SyncEvent event;
            event.type = SyncEventType::Apply;
            event.message = std::move(message);
            PushEvent(std::move(event));
        } else if (message.type == MessageType::SyncResponse) {
            SyncEvent event;
            event.type = SyncEventType::SyncResponse;
            event.message = std::move(message);
            PushEvent(std::move(event));
        } else if (message.type == MessageType::Error) {
            SyncEvent event;
            event.type = SyncEventType::Error;
            message.GetString(L"reason", &event.detail);
            event.message = std::move(message);
            PushEvent(std::move(event));
        }
    }
}

void SyncClient::HeartbeatLoop() {
    while (!stopping_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (stopping_ || !connected_) {
            continue;
        }

        const uint64_t now = NowTickMs();
        if (now - last_pong_tick_ms_.load() > kHeartbeatTimeoutMs) {
            last_error_ = L"heartbeat timeout";
            connected_ = false;
            CloseSocket();
            send_cv_.notify_all();

            SyncEvent event;
            event.type = SyncEventType::Disconnected;
            event.detail = last_error_;
            PushEvent(std::move(event));
            break;
        }

        if (now - last_ping_tick_ms_.load() >= kHeartbeatIntervalMs) {
            last_ping_tick_ms_ = now;
            SendPing(now);
        }
    }
}

void SyncClient::PushEvent(SyncEvent event) {
    SyncEventCallback callback;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        event_queue_.push(std::move(event));
        callback = event_callback_;
    }

    if (callback) {
        callback();
    }
}

void SyncClient::CloseSocket() {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

}  // namespace lightnote
