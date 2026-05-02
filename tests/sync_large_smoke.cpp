#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

bool WaitForCompleteSync(
    lightnote::SyncClient& client,
    uint64_t expected_revision,
    std::wstring* synced_text,
    int timeout_ms) {
    std::vector<std::wstring> chunks;
    std::vector<bool> received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        lightnote::SyncEvent event;
        while (client.TryPopEvent(&event)) {
            if (event.type != lightnote::SyncEventType::SyncResponse) {
                continue;
            }

            uint64_t revision = 0;
            uint64_t chunk_index = 0;
            uint64_t chunk_count = 1;
            std::wstring text;
            if (!event.message.GetNumber(L"rev", &revision) ||
                !event.message.GetString(L"text", &text) ||
                revision != expected_revision) {
                continue;
            }

            event.message.GetNumber(L"chunk_index", &chunk_index);
            event.message.GetNumber(L"chunk_count", &chunk_count);
            if (chunk_count == 0 || chunk_index >= chunk_count) {
                return false;
            }

            if (chunks.size() != chunk_count) {
                chunks.assign(static_cast<size_t>(chunk_count), std::wstring());
                received.assign(static_cast<size_t>(chunk_count), false);
            }

            chunks[static_cast<size_t>(chunk_index)] = std::move(text);
            received[static_cast<size_t>(chunk_index)] = true;

            const bool complete = std::all_of(
                received.begin(),
                received.end(),
                [](bool value) { return value; });
            if (complete) {
                synced_text->clear();
                for (const std::wstring& chunk : chunks) {
                    synced_text->append(chunk);
                }
                return true;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
}

}  // namespace

int wmain() {
    lightnote::WinSockRuntime winsock;
    if (!Expect(winsock.ok(), L"winsock startup failed")) return 1;

    std::wstring large_document;
    large_document.reserve(1024 * 1024);
    for (size_t index = 0; index < 1024 * 1024; ++index) {
        large_document.push_back(static_cast<wchar_t>(L'a' + (index % 26)));
    }

    lightnote::SyncServer server;
    server.SetDocument(large_document, 42);
    if (!Expect(server.Start(L"777001", 7777), L"server start failed")) return 1;

    lightnote::SyncClient client;
    if (!client.Connect(L"127.0.0.1", server.port(), L"777001", L"large-device", L"large-test")) {
        std::wcerr << L"client join failed last_error=" << client.last_error()
                   << L" join_fail_reason=" << client.join_fail_reason() << L"\n";
        return 1;
    }

    std::wstring synced_text;
    if (!Expect(WaitForCompleteSync(client, 42, &synced_text, 5000), L"large sync should complete")) return 1;
    if (!Expect(synced_text == large_document, L"large synced document mismatch")) return 1;

    client.Disconnect();
    server.Stop();

    std::wcout << L"sync large smoke passed\n";
    return 0;
}
