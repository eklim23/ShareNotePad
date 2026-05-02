#include "sync_server.h"

#include "document_model.h"
#include "protocol.h"
#include "text_encoding.h"

#include <objbase.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <windows.h>

namespace lightnote {
namespace {

constexpr DWORD kHandshakeTimeoutMs = 5000;
constexpr DWORD kSocketIoTimeoutMs = 1000;
constexpr size_t kSyncChunkChars = 8 * 1024;
constexpr size_t kMaxInsertChars = 32 * 1024;
constexpr int kMaxFailedJoinAttempts = 5;

enum class ReceiveResult {
    Message,
    Timeout,
    Closed,
    Error,
};

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

uint64_t NowTickMs() {
    return GetTickCount64();
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

std::wstring SessionLogTimestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t buffer[64]{};
    swprintf_s(
        buffer,
        L"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds);
    return buffer;
}

}  // namespace

SyncServer::SyncServer() = default;

SyncServer::~SyncServer() {
    Stop();
}

bool SyncServer::Start(std::wstring session_code, uint16_t preferred_port) {
    Stop();
    session_code_ = std::move(session_code);
    stopping_ = false;
    peer_joined_ = false;
    failed_join_count_ = 0;

    if (!BindListenSocket(preferred_port)) {
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&SyncServer::AcceptLoop, this);
    AppendSessionLog(L"START session=" + session_code_);
    return true;
}

void SyncServer::Stop() {
    if (running_) {
        AppendSessionLog(L"STOP session=" + session_code_);
    }

    stopping_ = true;
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseSocket(&listen_socket_);
        CloseSocket(&peer_socket_);
        peer_joined_ = false;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::vector<std::thread> client_threads;
    {
        std::lock_guard<std::mutex> lock(client_threads_mutex_);
        client_threads.swap(client_threads_);
    }

    for (std::thread& thread : client_threads) {
        if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
            thread.join();
        }
    }
}

void SyncServer::SetSessionLogPath(std::filesystem::path path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    session_log_path_ = std::move(path);
}

void SyncServer::SetDocument(std::wstring content, uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    document_ = std::move(content);
    revision_ = revision;
}

bool SyncServer::ApplyLocalOperation(
    const EditorOperation& operation,
    const std::wstring& client_id,
    ProtocolMessage* apply_message) {
    ProtocolMessage request;
    request.type = operation.type == EditorOperationType::Insert ? MessageType::Insert : MessageType::Delete;
    request.session = session_code_;
    request.client = client_id.empty() ? L"A" : client_id;
    request.SetNumber(L"base_rev", revision_);
    request.SetNumber(L"pos", static_cast<uint64_t>(operation.position));
    if (operation.type == EditorOperationType::Insert) {
        request.SetString(L"text", operation.text);
    } else {
        request.SetNumber(L"len", static_cast<uint64_t>(operation.length));
    }

    std::wstring error_reason;
    ProtocolMessage apply;
    if (!ApplyMessageToDocument(request, &apply, &error_reason)) {
        last_error_ = error_reason;
        return false;
    }

    BroadcastApply(apply);
    if (apply_message != nullptr) {
        *apply_message = std::move(apply);
    }
    return true;
}

void SyncServer::SetEventCallback(SyncEventCallback callback) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    event_callback_ = std::move(callback);
}

bool SyncServer::TryPopEvent(SyncEvent* event) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    if (event_queue_.empty()) {
        return false;
    }

    *event = std::move(event_queue_.front());
    event_queue_.pop();
    return true;
}

uint16_t SyncServer::port() const {
    return port_;
}

bool SyncServer::running() const {
    return running_;
}

uint64_t SyncServer::revision() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

std::wstring SyncServer::document() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return document_;
}

const std::wstring& SyncServer::last_error() const {
    return last_error_;
}

bool SyncServer::BindListenSocket(uint16_t preferred_port) {
    for (uint16_t candidate = preferred_port; candidate < preferred_port + 23 && candidate <= 7799; ++candidate) {
        SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCKET) {
            last_error_ = L"socket creation failed";
            return false;
        }

        BOOL exclusive = TRUE;
        setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(candidate);

        if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            listen(socket, 1) == 0) {
            listen_socket_ = socket;
            port_ = candidate;
            last_error_.clear();
            return true;
        }

        closesocket(socket);
    }

    last_error_ = L"no available port";
    return false;
}

void SyncServer::AcceptLoop() {
    while (!stopping_) {
        SOCKET client = accept(listen_socket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!stopping_) {
                last_error_ = L"accept failed";
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(client_threads_mutex_);
            client_threads_.emplace_back([this, client] {
                SetSocketTimeouts(client);
                SetTcpNoDelay(client);
                HandleClient(client);
            });
        }
    }
}

void SyncServer::HandleClient(SOCKET client_socket) {
    FrameDecoder decoder;
    bool joined_peer = false;

    ProtocolMessage hello;
    if (!ReceiveProtocolMessageWithin(client_socket, &decoder, &hello, kHandshakeTimeoutMs) ||
        hello.type != MessageType::Hello) {
        RegisterJoinFailure();
        SendJoinFail(client_socket, L"protocol_mismatch");
        closesocket(client_socket);
        return;
    }

    ProtocolMessage join;
    if (!ReceiveProtocolMessageWithin(client_socket, &decoder, &join, kHandshakeTimeoutMs) ||
        join.type != MessageType::Join) {
        RegisterJoinFailure();
        SendJoinFail(client_socket, L"protocol_mismatch");
        closesocket(client_socket);
        return;
    }

    if (IsJoinBlocked()) {
        SendJoinFail(client_socket, L"blocked");
        closesocket(client_socket);
        return;
    }

    std::wstring join_code;
    join.GetString(L"join_code", &join_code);
    if (join_code != session_code_) {
        const bool blocked = RegisterJoinFailure();
        SendJoinFail(client_socket, blocked ? L"blocked" : L"invalid_code");
        closesocket(client_socket);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (peer_joined_) {
            SendJoinFail(client_socket, L"room_full");
            closesocket(client_socket);
            return;
        }

        peer_joined_ = true;
        peer_socket_ = client_socket;
        joined_peer = true;
    }

    std::wstring reconnect_token;
    join.GetString(L"reconnect_token", &reconnect_token);
    const bool reconnecting = !reconnect_token.empty();

    if (!SendJoinOk(client_socket, reconnecting)) {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseSocket(&peer_socket_);
        peer_joined_ = false;
        return;
    }

    std::wstring peer_session = join.session;
    while (!stopping_) {
        ProtocolMessage message;
        const ReceiveResult result = ReceiveProtocolMessage(client_socket, &decoder, &message);
        if (result == ReceiveResult::Timeout) {
            continue;
        }

        if (result != ReceiveResult::Message) {
            break;
        }

        if (message.session != session_code_) {
            SendError(client_socket, L"invalid_session");
            continue;
        }

        if (message.type == MessageType::Ping) {
            ProtocolMessage pong;
            pong.type = MessageType::Pong;
            pong.session = peer_session.empty() ? session_code_ : peer_session;
            pong.client = L"server";

            uint64_t time = 0;
            if (message.GetNumber(L"time", &time)) {
                pong.SetNumber(L"time", time);
            }

            if (!SendProtocolMessage(client_socket, pong)) {
                break;
            }
        } else if (message.type == MessageType::Insert || message.type == MessageType::Delete) {
            if (!HandleEditMessage(client_socket, message)) {
                break;
            }
        } else if (message.type == MessageType::SyncRequest) {
            if (!SendSyncResponse(client_socket)) {
                break;
            }
        } else {
            SendError(client_socket, L"unsupported_message");
        }
    }

    bool should_close = true;
    std::lock_guard<std::mutex> lock(mutex_);
    if (peer_socket_ == client_socket) {
        peer_socket_ = INVALID_SOCKET;
        peer_joined_ = false;
    } else {
        should_close = false;
    }

    if (should_close) {
        closesocket(client_socket);
    }

    if (joined_peer && !stopping_) {
        SyncEvent event;
        event.type = SyncEventType::Disconnected;
        event.detail = L"peer disconnected";
        PushEvent(std::move(event));
    }
}

bool SyncServer::SendJoinOk(SOCKET client_socket, bool reconnecting) {
    ProtocolMessage response;
    response.type = MessageType::JoinOk;
    response.session = session_code_;
    response.client = L"server";
    response.SetString(L"assigned_client", L"B");
    response.SetString(L"reconnect_token", GenerateReconnectToken());
    response.SetNumber(L"current_rev", 0);
    AppendSessionLog(std::wstring(L"JOIN_OK assigned_client=B reconnect=") + (reconnecting ? L"1" : L"0"));
    return SendProtocolMessage(client_socket, response);
}

bool SyncServer::SendJoinFail(SOCKET client_socket, const std::wstring& reason) {
    ProtocolMessage response;
    response.type = MessageType::JoinFail;
    response.session = session_code_;
    response.client = L"server";
    response.SetString(L"reason", reason);
    AppendSessionLog(L"JOIN_FAIL reason=" + reason);
    return SendProtocolMessage(client_socket, response);
}

bool SyncServer::RegisterJoinFailure() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++failed_join_count_;
    return failed_join_count_ >= kMaxFailedJoinAttempts;
}

bool SyncServer::IsJoinBlocked() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_join_count_ >= kMaxFailedJoinAttempts;
}

bool SyncServer::SendSyncResponse(SOCKET client_socket) {
    std::wstring document;
    uint64_t revision = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        document = document_;
        revision = revision_;
    }

    const size_t chunk_count = std::max<size_t>(1, (document.size() + kSyncChunkChars - 1) / kSyncChunkChars);
    std::wostringstream log;
    log << L"SYNC_RESPONSE rev=" << revision << L" chunks=" << chunk_count;
    AppendSessionLog(log.str());

    for (size_t index = 0; index < chunk_count; ++index) {
        const size_t offset = index * kSyncChunkChars;
        const size_t length = std::min(kSyncChunkChars, document.size() - offset);

        ProtocolMessage response;
        response.type = MessageType::SyncResponse;
        response.session = session_code_;
        response.client = L"server";
        response.SetNumber(L"rev", revision);
        response.SetNumber(L"chunk_index", static_cast<uint64_t>(index));
        response.SetNumber(L"chunk_count", static_cast<uint64_t>(chunk_count));
        response.SetString(L"text", document.substr(offset, length));

        if (!SendProtocolMessage(client_socket, response)) {
            return false;
        }
    }

    return true;
}

bool SyncServer::SendError(SOCKET client_socket, const std::wstring& reason) {
    ProtocolMessage response;
    response.type = MessageType::Error;
    response.session = session_code_;
    response.client = L"server";
    response.SetString(L"reason", reason);
    return SendProtocolMessage(client_socket, response);
}

bool SyncServer::HandleEditMessage(SOCKET client_socket, const ProtocolMessage& message) {
    if (message.client != L"B") {
        AppendSessionLog(L"EDIT_REJECT reason=invalid_client");
        SendError(client_socket, L"invalid_client");
        return true;
    }

    ProtocolMessage apply;
    std::wstring error_reason;
    if (!ApplyMessageToDocument(message, &apply, &error_reason)) {
        AppendSessionLog(L"EDIT_REJECT reason=" + error_reason);
        SendError(client_socket, error_reason);
        return true;
    }

    SyncEvent event;
    event.type = SyncEventType::Apply;
    event.message = apply;
    PushEvent(std::move(event));

    if (!SendProtocolMessage(client_socket, apply)) {
        return false;
    }
    return true;
}

bool SyncServer::ApplyMessageToDocument(
    const ProtocolMessage& message,
    ProtocolMessage* apply_message,
    std::wstring* error_reason) {
    if (message.session != session_code_) {
        if (error_reason != nullptr) {
            *error_reason = L"invalid_session";
        }
        return false;
    }

    if (message.client != L"A" && message.client != L"B") {
        if (error_reason != nullptr) {
            *error_reason = L"invalid_client";
        }
        return false;
    }

    uint64_t position_value = 0;
    if (!message.GetNumber(L"pos", &position_value)) {
        if (error_reason != nullptr) {
            *error_reason = L"missing_position";
        }
        return false;
    }

    DocumentApplyStatus status = DocumentApplyStatus::InvalidOperation;
    std::wstring text;
    uint64_t length_value = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (position_value > static_cast<uint64_t>(document_.size())) {
            status = DocumentApplyStatus::InvalidPosition;
        } else if (message.type == MessageType::Insert) {
            if (!message.GetString(L"text", &text)) {
                status = DocumentApplyStatus::InvalidOperation;
            } else if (text.size() > kMaxInsertChars) {
                status = DocumentApplyStatus::InsertTooLarge;
            } else {
                status = ApplyInsertToContent(&document_, static_cast<size_t>(position_value), text);
            }
        } else if (message.type == MessageType::Delete) {
            if (!message.GetNumber(L"len", &length_value)) {
                status = DocumentApplyStatus::InvalidOperation;
            } else {
                status = ApplyDeleteToContent(
                    &document_,
                    static_cast<size_t>(position_value),
                    static_cast<size_t>(length_value));
            }
        }

        if (status != DocumentApplyStatus::Applied) {
            if (error_reason != nullptr) {
                *error_reason = DocumentApplyStatusToString(status);
            }
            return false;
        }

        ++revision_;
        apply_message->type = MessageType::Apply;
        apply_message->session = session_code_;
        apply_message->client = message.client;
        apply_message->SetNumber(L"rev", revision_);
        apply_message->SetNumber(L"pos", position_value);
        if (message.type == MessageType::Insert) {
            apply_message->SetString(L"op", L"INSERT");
            apply_message->SetString(L"text", text);
        } else {
            apply_message->SetString(L"op", L"DELETE");
            apply_message->SetNumber(L"len", length_value);
        }

        std::wostringstream log;
        log << L"APPLY rev=" << revision_
            << L" op=" << (message.type == MessageType::Insert ? L"INSERT" : L"DELETE")
            << L" client=" << message.client
            << L" pos=" << position_value;
        if (message.type == MessageType::Insert) {
            log << L" text_len=" << text.size();
        } else {
            log << L" len=" << length_value;
        }
        AppendSessionLog(log.str());
    }

    return true;
}

bool SyncServer::BroadcastApply(const ProtocolMessage& apply_message) {
    SOCKET socket = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        socket = peer_socket_;
    }

    if (socket == INVALID_SOCKET) {
        return true;
    }

    return SendProtocolMessage(socket, apply_message);
}

void SyncServer::PushEvent(SyncEvent event) {
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

bool SyncServer::AppendSessionLog(std::wstring_view message) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (session_log_path_.empty()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(session_log_path_.parent_path(), error);
    if (error) {
        last_error_ = L"session.log directory creation failed";
        return false;
    }

    std::wstring line = SessionLogTimestamp();
    line.push_back(L' ');
    line.append(message);
    line.push_back(L'\n');

    const std::string bytes = WideToUtf8(line);
    if (bytes.empty() && !line.empty()) {
        last_error_ = L"session.log encoding failed";
        return false;
    }

    std::ofstream file(session_log_path_, std::ios::binary | std::ios::app);
    if (!file) {
        last_error_ = L"session.log open failed";
        return false;
    }

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
        last_error_ = L"session.log write failed";
        return false;
    }

    return true;
}

std::wstring SyncServer::GenerateReconnectToken() const {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return L"local-token";
    }

    wchar_t buffer[39]{};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
        return L"local-token";
    }

    std::wstring token(buffer);
    if (token.size() >= 2 && token.front() == L'{' && token.back() == L'}') {
        token = token.substr(1, token.size() - 2);
    }
    return token;
}

void SyncServer::CloseSocket(SOCKET* socket) {
    if (*socket != INVALID_SOCKET) {
        shutdown(*socket, SD_BOTH);
        closesocket(*socket);
        *socket = INVALID_SOCKET;
    }
}

}  // namespace lightnote
