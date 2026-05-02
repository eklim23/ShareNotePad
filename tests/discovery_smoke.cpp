#include "discovery.h"
#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <iostream>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

}  // namespace

int wmain() {
    lightnote::WinSockRuntime winsock;
    if (!Expect(winsock.ok(), L"winsock startup failed")) return 1;

    lightnote::SyncServer server;
    if (!Expect(server.Start(L"482913", 7777), L"server start failed")) return 1;

    lightnote::DiscoveryAnnouncer announcer;
    if (!Expect(announcer.Start(L"482913", server.port()), L"discovery announcer failed")) return 1;

    lightnote::DiscoveryClient discovery;
    lightnote::DiscoveryEndpoint endpoint;
    if (!Expect(discovery.Find(L"482913", 2500, &endpoint), L"discovery find failed")) return 1;
    if (!Expect(!endpoint.host.empty() && endpoint.port == server.port(), L"discovery endpoint mismatch")) return 1;

    lightnote::SyncClient client;
    if (!Expect(client.Connect(endpoint.host, endpoint.port, L"482913", L"device-a", L"테스터"),
                L"client should join discovered room")) return 1;
    if (!Expect(client.connected(), L"client should be connected")) return 1;

    client.Disconnect();
    announcer.Stop();
    server.Stop();

    lightnote::SyncServer wrong_server;
    if (!Expect(wrong_server.Start(L"111111", 7777), L"wrong server start failed")) return 1;

    lightnote::DiscoveryAnnouncer wrong_announcer;
    if (!Expect(wrong_announcer.Start(L"111111", wrong_server.port()), L"wrong announcer failed")) return 1;

    lightnote::DiscoveryEndpoint wrong_endpoint;
    if (!Expect(!discovery.Find(L"222222", 350, &wrong_endpoint), L"discovery should ignore other sessions")) return 1;

    wrong_announcer.Stop();
    wrong_server.Stop();

    std::wcout << L"discovery smoke passed\n";
    return 0;
}

