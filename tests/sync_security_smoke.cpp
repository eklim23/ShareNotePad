#include "editor_diff.h"
#include "protocol.h"
#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <ws2tcpip.h>

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
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

bool SendMessage(SOCKET socket, const lightnote::ProtocolMessage& message) {
    std::string json;
    lightnote::ProtocolError error = lightnote::ProtocolError::None;
    if (!lightnote::SerializeMessage(message, &json, &error)) {
        return false;
    }
    return SendAll(socket, lightnote::EncodeFrame(json));
}

bool ReceiveMessage(SOCKET socket, lightnote::FrameDecoder* decoder, lightnote::ProtocolMessage* message) {
    char buffer[4096]{};
    const DWORD timeout_ms = 3000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));

    while (true) {
        std::string payload;
        lightnote::ProtocolError frame_error = lightnote::ProtocolError::None;
        if (decoder->TryReadFrame(&payload, &frame_error)) {
            lightnote::ProtocolError message_error = lightnote::ProtocolError::None;
            return lightnote::DeserializeMessage(payload, message, &message_error);
        }
        if (frame_error != lightnote::ProtocolError::None) {
            return false;
        }

        const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
        if (received <= 0) {
            return false;
        }
        if (!decoder->Append(std::string_view(buffer, static_cast<size_t>(received)), &frame_error)) {
            return false;
        }
    }
}

SOCKET ConnectSocket(uint16_t port) {
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonW(AF_INET, L"127.0.0.1", &address.sin_addr) != 1) {
        closesocket(socket);
        return INVALID_SOCKET;
    }

    if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(socket);
        return INVALID_SOCKET;
    }

    return socket;
}

bool RawJoin(SOCKET socket, const std::wstring& session_code, lightnote::FrameDecoder* decoder) {
    lightnote::ProtocolMessage hello;
    hello.type = lightnote::MessageType::Hello;
    hello.session = session_code;
    hello.client = L"B";
    hello.SetNumber(L"protocol", 1);
    hello.SetString(L"device_id", L"raw-device");
    hello.SetString(L"nickname", L"raw");

    lightnote::ProtocolMessage join;
    join.type = lightnote::MessageType::Join;
    join.session = session_code;
    join.client = L"B";
    join.SetString(L"join_code", session_code);
    join.SetString(L"reconnect_token", L"");

    if (!SendMessage(socket, hello) || !SendMessage(socket, join)) {
        return false;
    }

    lightnote::ProtocolMessage response;
    return ReceiveMessage(socket, decoder, &response) && response.type == lightnote::MessageType::JoinOk;
}

bool ExpectErrorReason(SOCKET socket, lightnote::FrameDecoder* decoder, const std::wstring& reason) {
    lightnote::ProtocolMessage response;
    if (!ReceiveMessage(socket, decoder, &response)) {
        return false;
    }

    std::wstring actual;
    return response.type == lightnote::MessageType::Error &&
           response.GetString(L"reason", &actual) &&
           actual == reason;
}

}  // namespace

int wmain() {
    lightnote::WinSockRuntime winsock;
    if (!Expect(winsock.ok(), L"winsock startup failed")) return 1;

    lightnote::SyncServer blocked_server;
    if (!Expect(blocked_server.Start(L"900001", 7777), L"blocked server start failed")) return 1;

    for (int attempt = 1; attempt <= 5; ++attempt) {
        lightnote::SyncClient wrong_client;
        if (!Expect(!wrong_client.Connect(L"127.0.0.1", blocked_server.port(), L"wrong", L"device", L"tester"),
                    L"wrong join should fail")) return 1;
        const std::wstring expected = attempt >= 5 ? L"blocked" : L"invalid_code";
        if (!Expect(wrong_client.join_fail_reason() == expected, L"wrong join reason mismatch")) return 1;
    }

    lightnote::SyncClient blocked_valid_client;
    if (!Expect(!blocked_valid_client.Connect(L"127.0.0.1", blocked_server.port(), L"900001", L"device", L"tester"),
                L"valid join should fail after block")) return 1;
    if (blocked_valid_client.join_fail_reason() != L"blocked") {
        std::wcout << L"blocked reason mismatch: " << blocked_valid_client.join_fail_reason() << L"\n";
        return 1;
    }
    blocked_server.Stop();

    lightnote::SyncServer edit_server;
    if (!Expect(edit_server.Start(L"900002", 7777), L"edit server start failed")) return 1;
    lightnote::SyncClient edit_client;
    if (!Expect(edit_client.Connect(L"127.0.0.1", edit_server.port(), L"900002", L"device", L"tester"),
                L"edit client should connect")) return 1;

    std::wstring too_large_insert(32 * 1024 + 1, L'x');
    if (!Expect(edit_client.SendInsert(0, 0, too_large_insert), L"large insert should send")) return 1;

    bool saw_insert_too_large = false;
    const auto deadline = GetTickCount64() + 3000;
    while (GetTickCount64() < deadline && !saw_insert_too_large) {
        lightnote::SyncEvent event;
        while (edit_client.TryPopEvent(&event)) {
            if (event.type == lightnote::SyncEventType::Error && event.detail == L"insert_too_large") {
                saw_insert_too_large = true;
                break;
            }
        }
        Sleep(20);
    }
    if (!Expect(saw_insert_too_large, L"large insert should be rejected")) return 1;
    edit_client.Disconnect();
    edit_server.Stop();

    lightnote::SyncServer raw_server;
    if (!Expect(raw_server.Start(L"900003", 7777), L"raw server start failed")) return 1;
    SOCKET raw_socket = ConnectSocket(raw_server.port());
    if (!Expect(raw_socket != INVALID_SOCKET, L"raw socket should connect")) return 1;
    lightnote::FrameDecoder raw_decoder;
    if (!Expect(RawJoin(raw_socket, L"900003", &raw_decoder), L"raw join should succeed")) return 1;

    lightnote::ProtocolMessage wrong_session_insert;
    wrong_session_insert.type = lightnote::MessageType::Insert;
    wrong_session_insert.session = L"bad-session";
    wrong_session_insert.client = L"B";
    wrong_session_insert.SetNumber(L"base_rev", 0);
    wrong_session_insert.SetNumber(L"pos", 0);
    wrong_session_insert.SetString(L"text", L"x");
    if (!Expect(SendMessage(raw_socket, wrong_session_insert), L"wrong session insert should send")) return 1;
    if (!Expect(ExpectErrorReason(raw_socket, &raw_decoder, L"invalid_session"),
                L"wrong session should be rejected")) return 1;

    lightnote::ProtocolMessage wrong_client_insert;
    wrong_client_insert.type = lightnote::MessageType::Insert;
    wrong_client_insert.session = L"900003";
    wrong_client_insert.client = L"X";
    wrong_client_insert.SetNumber(L"base_rev", 0);
    wrong_client_insert.SetNumber(L"pos", 0);
    wrong_client_insert.SetString(L"text", L"x");
    if (!Expect(SendMessage(raw_socket, wrong_client_insert), L"wrong client insert should send")) return 1;
    if (!Expect(ExpectErrorReason(raw_socket, &raw_decoder, L"invalid_client"),
                L"wrong client should be rejected")) return 1;

    closesocket(raw_socket);
    raw_server.Stop();

    lightnote::SyncServer limit_server;
    limit_server.SetDocument(std::wstring(lightnote::kMaxDocumentChars, L'z'), 0);
    lightnote::EditorOperation operation;
    operation.type = lightnote::EditorOperationType::Insert;
    operation.position = lightnote::kMaxDocumentChars;
    operation.text = L"x";
    lightnote::ProtocolMessage ignored_apply;
    if (!Expect(!limit_server.ApplyLocalOperation(operation, L"A", &ignored_apply),
                L"full document should reject one more character")) return 1;
    if (!Expect(limit_server.last_error() == L"document_too_large", L"5MB limit reason mismatch")) return 1;

    std::wcout << L"sync security smoke passed\n";
    return 0;
}
