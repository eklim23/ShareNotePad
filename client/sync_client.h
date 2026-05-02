#pragma once

#include "protocol.h"
#include "sync_events.h"

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <winsock2.h>

namespace lightnote {

class SyncClient {
public:
    SyncClient();
    ~SyncClient();

    SyncClient(const SyncClient&) = delete;
    SyncClient& operator=(const SyncClient&) = delete;

    bool Connect(
        const std::wstring& host,
        uint16_t port,
        const std::wstring& session_code,
        const std::wstring& device_id,
        const std::wstring& nickname);
    void Disconnect();
    bool SendPing(uint64_t timestamp);
    bool SendInsert(uint64_t base_revision, size_t position, std::wstring text);
    bool SendDelete(uint64_t base_revision, size_t position, size_t length);
    bool SendSyncRequest(uint64_t current_revision);
    void SetEventCallback(SyncEventCallback callback);
    bool TryPopEvent(SyncEvent* event);

    bool connected() const;
    uint64_t pong_count() const;
    const std::wstring& assigned_client() const;
    const std::wstring& reconnect_token() const;
    const std::wstring& join_fail_reason() const;
    const std::wstring& last_error() const;

private:
    bool SendHelloAndJoin(
        const std::wstring& session_code,
        const std::wstring& device_id,
        const std::wstring& nickname);
    bool ReadJoinResponse();
    bool EnqueueMessage(const ProtocolMessage& message);
    void StartWorkers();
    void StopWorkers();
    void SendLoop();
    void ReceiveLoop();
    void HeartbeatLoop();
    void CloseSocket();
    void PushEvent(SyncEvent event);

    SOCKET socket_ = INVALID_SOCKET;
    std::atomic_bool connected_ = false;
    std::atomic_bool stopping_ = false;
    std::atomic_uint64_t pong_count_ = 0;
    std::atomic_uint64_t last_pong_tick_ms_ = 0;
    std::atomic_uint64_t last_ping_tick_ms_ = 0;
    std::thread send_thread_;
    std::thread receive_thread_;
    std::thread heartbeat_thread_;
    mutable std::mutex socket_mutex_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    std::queue<std::string> send_queue_;
    std::mutex event_mutex_;
    std::queue<SyncEvent> event_queue_;
    SyncEventCallback event_callback_;
    std::wstring session_code_;
    std::wstring assigned_client_;
    std::wstring reconnect_token_;
    std::wstring join_fail_reason_;
    std::wstring last_error_;
};

}  // namespace lightnote
