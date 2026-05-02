#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return predicate();
}

bool WaitForClientEvent(
    lightnote::SyncClient& client,
    lightnote::SyncEventType type,
    lightnote::SyncEvent* event,
    int timeout_ms) {
    return WaitUntil([&] {
        lightnote::SyncEvent next;
        while (client.TryPopEvent(&next)) {
            if (next.type == type) {
                *event = std::move(next);
                return true;
            }
        }
        return false;
    }, timeout_ms);
}

}  // namespace

int wmain() {
    lightnote::WinSockRuntime winsock;
    if (!Expect(winsock.ok(), L"winsock startup failed")) return 1;

    const std::filesystem::path session_log =
        std::filesystem::temp_directory_path() / L"ShareNotepadSyncSmoke" / L"session.log";
    std::error_code remove_error;
    std::filesystem::remove(session_log, remove_error);

    lightnote::SyncServer server;
    server.SetSessionLogPath(session_log);
    if (!Expect(server.Start(L"482913", 7777), L"server start failed")) return 1;

    lightnote::SyncClient client;
    if (!client.Connect(L"127.0.0.1", server.port(), L"482913", L"device-a", L"tester")) {
        std::wcerr << L"client join should succeed"
                   << L" last_error=" << client.last_error()
                   << L" join_fail_reason=" << client.join_fail_reason()
                   << L" server_error=" << server.last_error()
                   << L" port=" << server.port() << L"\n";
        return 1;
    }
    if (!Expect(client.connected(), L"client should be connected")) return 1;
    if (!Expect(client.assigned_client() == L"B", L"assigned client mismatch")) return 1;
    if (!Expect(!client.reconnect_token().empty(), L"reconnect token missing")) return 1;
    const std::wstring first_reconnect_token = client.reconnect_token();
    lightnote::SyncEvent sync_response;
    if (!Expect(WaitForClientEvent(client, lightnote::SyncEventType::SyncResponse, &sync_response, 1000),
                L"client should receive initial sync")) return 1;
    uint64_t synced_revision = 99;
    std::wstring synced_text = L"not-empty";
    if (!Expect(sync_response.message.GetNumber(L"rev", &synced_revision), L"sync response revision missing")) return 1;
    if (!Expect(sync_response.message.GetString(L"text", &synced_text), L"sync response text missing")) return 1;
    if (!Expect(synced_revision == 0 && synced_text.empty(), L"initial sync mismatch")) return 1;

    std::this_thread::sleep_for(std::chrono::milliseconds(6200));
    if (!Expect(client.connected(), L"client should stay connected while idle")) return 1;
    if (!Expect(client.pong_count() > 0, L"heartbeat should receive pong")) return 1;

    if (!Expect(client.SendInsert(0, 0, L"hi"), L"client should send insert")) return 1;
    lightnote::SyncEvent insert_apply;
    if (!Expect(WaitForClientEvent(client, lightnote::SyncEventType::Apply, &insert_apply, 1000),
                L"client should receive insert apply")) return 1;
    uint64_t insert_revision = 0;
    std::wstring insert_op;
    std::wstring insert_text;
    if (!Expect(insert_apply.message.GetNumber(L"rev", &insert_revision), L"insert apply revision missing")) return 1;
    if (!Expect(insert_apply.message.GetString(L"op", &insert_op), L"insert apply op missing")) return 1;
    if (!Expect(insert_apply.message.GetString(L"text", &insert_text), L"insert apply text missing")) return 1;
    if (!Expect(insert_revision == 1 && insert_op == L"INSERT" && insert_text == L"hi",
                L"insert apply mismatch")) return 1;
    if (!Expect(server.document() == L"hi" && server.revision() == 1, L"server insert state mismatch")) return 1;

    if (!Expect(client.SendDelete(1, 1, 1), L"client should send delete")) return 1;
    lightnote::SyncEvent delete_apply;
    if (!Expect(WaitForClientEvent(client, lightnote::SyncEventType::Apply, &delete_apply, 1000),
                L"client should receive delete apply")) return 1;
    uint64_t delete_revision = 0;
    std::wstring delete_op;
    uint64_t delete_length = 0;
    if (!Expect(delete_apply.message.GetNumber(L"rev", &delete_revision), L"delete apply revision missing")) return 1;
    if (!Expect(delete_apply.message.GetString(L"op", &delete_op), L"delete apply op missing")) return 1;
    if (!Expect(delete_apply.message.GetNumber(L"len", &delete_length), L"delete apply len missing")) return 1;
    if (!Expect(delete_revision == 2 && delete_op == L"DELETE" && delete_length == 1,
                L"delete apply mismatch")) return 1;
    if (!Expect(server.document() == L"h" && server.revision() == 2, L"server delete state mismatch")) return 1;

    lightnote::SyncClient extra_client;
    if (!Expect(!extra_client.Connect(L"127.0.0.1", server.port(), L"482913", L"device-extra", L"extra"),
                L"second peer should be rejected")) return 1;
    if (!Expect(extra_client.join_fail_reason() == L"room_full", L"room full failure reason mismatch")) return 1;

    const uint64_t pong_count_before_manual_ping = client.pong_count();
    if (!Expect(client.SendPing(123), L"client should enqueue ping")) return 1;
    if (!Expect(WaitUntil([&client, pong_count_before_manual_ping] {
            return client.pong_count() > pong_count_before_manual_ping;
        }, 1000),
                L"client should receive manual pong")) return 1;
    const uint16_t original_port = server.port();
    server.Stop();
    if (!Expect(WaitUntil([&client] { return !client.connected(); }, 1000), L"client should detect disconnect")) return 1;

    lightnote::SyncServer reconnect_server;
    reconnect_server.SetSessionLogPath(session_log);
    reconnect_server.SetDocument(L"reconnected", 7);
    if (!Expect(reconnect_server.Start(L"482913", original_port), L"reconnect server start failed")) return 1;
    if (!Expect(client.Connect(L"127.0.0.1", reconnect_server.port(), L"482913", L"device-a", L"tester"),
                L"client should reconnect")) return 1;
    if (!Expect(client.reconnect_token() != first_reconnect_token, L"reconnect should rotate token")) return 1;
    lightnote::SyncEvent reconnect_sync_response;
    if (!Expect(WaitForClientEvent(client, lightnote::SyncEventType::SyncResponse, &reconnect_sync_response, 1000),
                L"client should receive reconnect sync")) return 1;
    uint64_t reconnect_revision = 0;
    std::wstring reconnect_text;
    if (!Expect(reconnect_sync_response.message.GetNumber(L"rev", &reconnect_revision),
                L"reconnect sync revision missing")) return 1;
    if (!Expect(reconnect_sync_response.message.GetString(L"text", &reconnect_text),
                L"reconnect sync text missing")) return 1;
    if (!Expect(reconnect_revision == 7 && reconnect_text == L"reconnected",
                L"reconnect sync mismatch")) return 1;
    client.Disconnect();
    reconnect_server.Stop();

    std::ifstream log_file(session_log, std::ios::binary);
    if (!Expect(static_cast<bool>(log_file), L"session log should exist")) return 1;
    const std::string session_log_bytes((std::istreambuf_iterator<char>(log_file)), std::istreambuf_iterator<char>());
    if (!Expect(session_log_bytes.find("JOIN_OK") != std::string::npos, L"session log should contain JOIN_OK")) return 1;
    if (!Expect(session_log_bytes.find("APPLY rev=1 op=INSERT") != std::string::npos,
                L"session log should contain insert apply")) return 1;
    if (!Expect(session_log_bytes.find("APPLY rev=2 op=DELETE") != std::string::npos,
                L"session log should contain delete apply")) return 1;
    if (!Expect(session_log_bytes.find("JOIN_FAIL reason=room_full") != std::string::npos,
                L"session log should contain room_full")) return 1;
    if (!Expect(session_log_bytes.find("JOIN_OK assigned_client=B reconnect=1") != std::string::npos,
                L"session log should contain reconnect join")) return 1;
    if (!Expect(session_log_bytes.find("reconnect_token") == std::string::npos,
                L"session log should not contain raw reconnect token field")) return 1;

    lightnote::SyncServer fail_server;
    if (!Expect(fail_server.Start(L"111111", 7777), L"fail server start failed")) return 1;

    lightnote::SyncClient wrong_client;
    if (!Expect(!wrong_client.Connect(L"127.0.0.1", fail_server.port(), L"222222", L"device-b", L"tester"),
                L"wrong code should fail")) return 1;
    if (!Expect(wrong_client.join_fail_reason() == L"invalid_code", L"wrong failure reason")) return 1;
    fail_server.Stop();

    lightnote::SyncServer first;
    lightnote::SyncServer second;
    if (!Expect(first.Start(L"333333", 7777), L"first fallback server failed")) return 1;
    if (!Expect(second.Start(L"444444", 7777), L"second fallback server failed")) return 1;
    if (!Expect(first.port() != second.port(), L"port fallback did not choose a new port")) return 1;
    second.Stop();
    first.Stop();

    std::wcout << L"sync smoke passed\n";
    return 0;
}
