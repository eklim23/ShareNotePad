#pragma once

#include "config.h"
#include "discovery.h"
#include "editor_diff.h"
#include "logger.h"
#include "storage.h"
#include "sync_client.h"
#include "sync_server.h"
#include "winsock_runtime.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>

namespace lightnote {

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    MainWindow(HINSTANCE instance, Config* config, Logger* logger);

    bool Create();
    void Show(int show_command);

private:
    enum class Screen {
        Start,
        Join,
        Editor,
    };

    enum class SyncRole {
        Offline,
        Host,
        Guest,
    };

    struct TabState {
        std::wstring title;
        std::wstring content;
        bool dirty = false;
    };

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool RegisterWindowClass();
    bool CreateMainMenu();
    bool CreateControls();
    void LoadInitialDocument();
    void InitializeTabs();
    void SyncActiveTabFromEditor();
    void SwitchToTab(size_t index);
    void HandleNewTab();
    void UpdateTabBar();
    std::wstring TabTitleText(size_t index) const;
    void ApplyDefaultFont(HWND control);
    void ApplyDarkWindowFrame();
    void ApplyDarkEditorTheme();
    void Layout();
    void ShowStartScreen();
    void ShowJoinScreen();
    void ShowEditorScreen(std::wstring_view status_text);
    void SetChildVisible(HWND control, bool visible);
    void OnEditorChanged();
    void ProcessEditorChange();
    void SendLocalOperations(const EditorDiffResult& diff);
    void DrainSyncEvents();
    void HandleSyncEvent(const SyncEvent& event);
    void StartReconnectLoop();
    void StopReconnectLoop();
    void ReconnectLoop();
    void HandleReconnectStatus(WPARAM wparam, LPARAM lparam);
    void ApplySyncResponse(const ProtocolMessage& message);
    void ApplyFullSyncDocument(std::wstring text, uint64_t revision);
    void ApplySyncApply(const ProtocolMessage& message);
    void RequestFullSync();
    void SaveEditorContent();
    std::wstring GetEditorText() const;
    std::wstring GetControlText(HWND control) const;
    bool IsOwnedEditControl(HWND control) const;
    HWND FocusedEditControl() const;
    void HandleNewDocument();
    void HandleEditCommand(int command_id);
    void HandleCreateRoom();
    void HandleJoinRoom();
    void HandleLeaveRoom();
    void CopyInviteCode();
    void ShowAboutDialog();
    void ShowPopupMenu(HMENU menu, HWND anchor);
    void ToggleMarkdownPreview();
    void UpdateMarkdownPreview();
    std::wstring RenderMarkdownPreview(std::wstring_view markdown) const;
    void ApplyMarkdownShortcutIfNeeded();
    void ResetMarkdownStyleAtLineBreakIfNeeded();
    void ApplyHeadingShortcut(size_t line_start, size_t marker_length, LONG twip_height);
    void ApplyBulletShortcut(size_t line_start, size_t marker_length);
    void ApplyQuoteShortcut(size_t line_start, size_t marker_length);
    void ApplyCodeShortcut(size_t line_start, size_t marker_length);
    void DeleteMarkdownMarker(size_t line_start, size_t marker_length);
    void SetRichEditInsertionFormat(LONG twip_height, bool bold, bool italic, const wchar_t* face_name);
    void ResetRichEditParagraphFormat();
    void ResetRichEditInsertionFormat();
    void HideJoinControls();
    void SetJoinControlsVisible(bool visible);
    void ToggleAdvancedJoin();
    void UpdateMenuState();
    std::wstring CurrentEditorStatusText() const;
    std::wstring FriendlyJoinFailureMessage(std::wstring reason) const;
    std::wstring FriendlySyncErrorMessage(std::wstring reason) const;
    std::wstring GenerateSessionCode() const;

    HINSTANCE instance_;
    Config* config_ = nullptr;
    Storage storage_;
    Logger* logger_ = nullptr;
    WinSockRuntime winsock_;
    SyncServer server_;
    SyncClient client_;
    DiscoveryAnnouncer discovery_announcer_;
    std::thread reconnect_thread_;
    std::atomic_bool reconnecting_ = false;
    std::atomic_bool reconnect_stop_ = false;
    HWND hwnd_ = nullptr;
    HMENU menu_ = nullptr;
    HMENU file_menu_ = nullptr;
    HMENU edit_menu_ = nullptr;
    HMENU view_menu_ = nullptr;
    HMENU share_menu_ = nullptr;
    HBRUSH app_background_brush_ = nullptr;
    HBRUSH toolbar_brush_ = nullptr;
    HBRUSH active_tab_brush_ = nullptr;
    HBRUSH inactive_tab_brush_ = nullptr;
    HBRUSH editor_background_brush_ = nullptr;
    HBRUSH status_brush_ = nullptr;
    HFONT ui_font_ = nullptr;
    HFONT toolbar_font_ = nullptr;
    HFONT editor_font_ = nullptr;
    HWND tab_bar_ = nullptr;
    HWND active_tab_label_ = nullptr;
    HWND inactive_tab_label_ = nullptr;
    HWND third_tab_label_ = nullptr;
    HWND new_tab_button_ = nullptr;
    HWND toolbar_bar_ = nullptr;
    HWND file_menu_button_ = nullptr;
    HWND edit_menu_button_ = nullptr;
    HWND view_menu_button_ = nullptr;
    HWND style_button_ = nullptr;
    HWND list_button_ = nullptr;
    HWND bold_button_ = nullptr;
    HWND italic_button_ = nullptr;
    HWND strike_button_ = nullptr;
    HWND link_button_ = nullptr;
    HWND table_button_ = nullptr;
    HWND markdown_button_ = nullptr;
    HWND share_button_ = nullptr;
    HWND account_button_ = nullptr;
    HWND settings_button_ = nullptr;
    HWND title_label_ = nullptr;
    HWND host_button_ = nullptr;
    HWND join_button_ = nullptr;
    HWND recent_label_ = nullptr;
    HWND join_title_label_ = nullptr;
    HWND join_code_label_ = nullptr;
    HWND join_code_edit_ = nullptr;
    HWND join_host_label_ = nullptr;
    HWND join_host_edit_ = nullptr;
    HWND join_port_label_ = nullptr;
    HWND join_port_edit_ = nullptr;
    HWND connect_button_ = nullptr;
    HWND back_button_ = nullptr;
    HWND advanced_button_ = nullptr;
    HWND editor_ = nullptr;
    HWND markdown_preview_ = nullptr;
    HWND status_label_ = nullptr;
    Screen screen_ = Screen::Start;
    bool advanced_join_visible_ = false;
    bool dirty_ = false;
    bool suppress_editor_change_ = false;
    bool ime_composing_ = false;
    bool remote_applying_ = false;
    bool markdown_preview_visible_ = false;
    bool markdown_shortcut_applying_ = false;
    SyncRole sync_role_ = SyncRole::Offline;
    std::wstring session_code_;
    std::wstring local_client_id_ = L"A";
    std::wstring reconnect_host_;
    uint16_t reconnect_port_ = 0;
    std::wstring reconnect_device_id_;
    std::wstring reconnect_nickname_;
    uint64_t current_revision_ = 0;
    uint64_t sync_response_revision_ = 0;
    std::vector<std::wstring> sync_response_chunks_;
    std::vector<bool> sync_response_received_;
    std::wstring last_known_text_;
    std::vector<TabState> tabs_;
    size_t active_tab_index_ = 0;
    int tab_label_width_ = 190;
};

}  // namespace lightnote
