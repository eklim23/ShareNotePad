#pragma once

#include "config.h"
#include "discovery.h"
#include "editor_diff.h"
#include "logger.h"
#include "storage.h"
#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <windows.h>
#include <unknwn.h>
#include <WebView2.h>
#include <wrl.h>
#include <wrl/client.h>

#include <string>
#include <string_view>
#include <vector>

namespace lightnote {

class WebViewWindow {
public:
    WebViewWindow(HINSTANCE instance, Config* config, Logger* logger);
    ~WebViewWindow();

    bool Create();
    void Show(int show_command);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool RegisterWindowClass();
    void InitializeWebView();
    void ResizeWebView();
    void ApplyDarkWindowFrame();
    void HandleWebMessage(std::wstring_view message);
    void HandleCreateRoom(std::wstring_view message);
    void HandleJoinRoom(std::wstring_view message);
    void DrainSyncEvents();
    void HandleSyncEvent(const SyncEvent& event);
    void SendLocalOperations(const EditorDiffResult& diff);
    void ApplySyncResponse(const ProtocolMessage& message);
    void ApplyFullSyncDocument(std::wstring text, uint64_t revision);
    void ApplySyncApply(const ProtocolMessage& message);
    void RequestFullSync();
    void PostInitialState();
    void PostStatus(std::wstring_view save_status, std::wstring_view connection_status);
    void PostRoomCreated(std::wstring_view code);
    void PostDocument(std::wstring_view text);
    void ShowWebViewError(HRESULT result);
    std::wstring IndexUri() const;
    std::wstring GenerateSessionCode() const;

    enum class SyncRole {
        Offline,
        Host,
        Guest,
    };

    HINSTANCE instance_;
    Config* config_ = nullptr;
    Logger* logger_ = nullptr;
    Storage storage_;
    WinSockRuntime winsock_;
    SyncServer server_;
    SyncClient client_;
    DiscoveryAnnouncer discovery_announcer_;
    HWND hwnd_ = nullptr;
    bool com_initialized_ = false;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
    SyncRole sync_role_ = SyncRole::Offline;
    std::wstring session_code_;
    std::wstring local_client_id_ = L"A";
    uint64_t current_revision_ = 0;
    uint64_t sync_response_revision_ = 0;
    std::vector<std::wstring> sync_response_chunks_;
    std::vector<bool> sync_response_received_;
    std::wstring last_known_text_;
};

}  // namespace lightnote
