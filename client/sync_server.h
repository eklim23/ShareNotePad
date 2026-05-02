#pragma once

#include "editor_diff.h"
#include "protocol.h"
#include "sync_events.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <winsock2.h>

namespace lightnote {

class SyncServer {
public:
    SyncServer();
    ~SyncServer();

    SyncServer(const SyncServer&) = delete;
    SyncServer& operator=(const SyncServer&) = delete;

    bool Start(std::wstring session_code, uint16_t preferred_port = 7777);
    void Stop();
    void SetSessionLogPath(std::filesystem::path path);
    void SetDocument(std::wstring content, uint64_t revision = 0);
    bool ApplyLocalOperation(const EditorOperation& operation, const std::wstring& client_id, ProtocolMessage* apply_message);
    void SetEventCallback(SyncEventCallback callback);
    bool TryPopEvent(SyncEvent* event);

    uint16_t port() const;
    bool running() const;
    uint64_t revision() const;
    std::wstring document() const;
    const std::wstring& last_error() const;

private:
    void AcceptLoop();
    void HandleClient(SOCKET client_socket);
    bool BindListenSocket(uint16_t preferred_port);
    bool SendJoinOk(SOCKET client_socket, bool reconnecting);
    bool SendJoinFail(SOCKET client_socket, const std::wstring& reason);
    bool RegisterJoinFailure();
    bool IsJoinBlocked() const;
    bool SendSyncResponse(SOCKET client_socket);
    bool SendError(SOCKET client_socket, const std::wstring& reason);
    bool HandleEditMessage(SOCKET client_socket, const ProtocolMessage& message);
    bool ApplyMessageToDocument(const ProtocolMessage& message, ProtocolMessage* apply_message, std::wstring* error_reason);
    bool BroadcastApply(const ProtocolMessage& apply_message);
    void PushEvent(SyncEvent event);
    bool AppendSessionLog(std::wstring_view message);
    std::wstring GenerateReconnectToken() const;
    void CloseSocket(SOCKET* socket);

    std::wstring session_code_;
    uint16_t port_ = 0;
    std::wstring document_;
    uint64_t revision_ = 0;
    SOCKET listen_socket_ = INVALID_SOCKET;
    SOCKET peer_socket_ = INVALID_SOCKET;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    std::atomic_bool running_ = false;
    std::atomic_bool stopping_ = false;
    bool peer_joined_ = false;
    int failed_join_count_ = 0;
    mutable std::mutex mutex_;
    std::mutex client_threads_mutex_;
    std::mutex event_mutex_;
    std::mutex log_mutex_;
    std::queue<SyncEvent> event_queue_;
    SyncEventCallback event_callback_;
    std::filesystem::path session_log_path_;
    std::wstring last_error_;
};

}  // namespace lightnote
