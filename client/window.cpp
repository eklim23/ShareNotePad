#include "window.h"

#include "document_model.h"
#include "resource.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <dwmapi.h>
#include <iomanip>
#include <random>
#include <richedit.h>
#include <sstream>
#include <thread>
#include <uxtheme.h>
#include <windowsx.h>

namespace lightnote {
namespace {

constexpr wchar_t kWindowClassName[] = L"ShareNotepadMainWindow";
constexpr wchar_t kWindowTitle[] = L"ShareNotepad";

constexpr int kHostButtonId = 1001;
constexpr int kJoinButtonId = 1002;
constexpr int kConnectButtonId = 1003;
constexpr int kBackButtonId = 1004;
constexpr int kAdvancedButtonId = 1005;
constexpr int kFileNewId = 1101;
constexpr int kFileSaveId = 1102;
constexpr int kFileExitId = 1103;
constexpr int kEditUndoId = 1201;
constexpr int kEditCutId = 1202;
constexpr int kEditCopyId = 1203;
constexpr int kEditPasteId = 1204;
constexpr int kEditDeleteId = 1205;
constexpr int kEditSelectAllId = 1206;
constexpr int kViewMarkdownPreviewId = 1251;
constexpr int kShareCopyCodeId = 1301;
constexpr int kShareLeaveRoomId = 1302;
constexpr int kHelpAboutId = 1401;
constexpr int kFileMenuButtonId = 1501;
constexpr int kEditMenuButtonId = 1502;
constexpr int kViewMenuButtonId = 1503;
constexpr int kStyleButtonId = 1504;
constexpr int kListButtonId = 1505;
constexpr int kBoldButtonId = 1506;
constexpr int kItalicButtonId = 1507;
constexpr int kStrikeButtonId = 1508;
constexpr int kLinkButtonId = 1509;
constexpr int kTableButtonId = 1510;
constexpr int kMarkdownButtonId = 1511;
constexpr int kShareButtonId = 1512;
constexpr int kAccountButtonId = 1513;
constexpr int kSettingsButtonId = 1514;
constexpr int kTab0ButtonId = 1601;
constexpr int kTab1ButtonId = 1602;
constexpr int kTab2ButtonId = 1603;
constexpr int kNewTabButtonId = 1604;
constexpr size_t kMaxTabs = 3;
constexpr int kMargin = 16;
constexpr int kButtonWidth = 160;
constexpr int kButtonHeight = 34;
constexpr int kLabelWidth = 92;
constexpr int kInputWidth = 220;
constexpr int kInputHeight = 24;
constexpr int kStatusHeight = 28;
constexpr int kTabBarHeight = 42;
constexpr int kToolbarHeight = 44;
constexpr int kTopChromeHeight = kTabBarHeight + kToolbarHeight;
constexpr LONG kDefaultTextHeight = 220;
constexpr LONG kHeading1Height = 320;
constexpr LONG kHeading2Height = 280;
constexpr LONG kHeading3Height = 250;
constexpr UINT_PTR kAutosaveTimerId = 2001;
constexpr UINT kAutosaveDelayMs = 1500;
constexpr UINT kSyncEventMessage = WM_APP + 1;
constexpr UINT kReconnectStatusMessage = WM_APP + 2;
constexpr int kMaxReconnectAttempts = 5;
constexpr COLORREF kDarkBackground = RGB(32, 32, 32);
constexpr COLORREF kDarkToolbar = RGB(38, 38, 38);
constexpr COLORREF kDarkTab = RGB(62, 62, 62);
constexpr COLORREF kInactiveTab = RGB(36, 36, 36);
constexpr COLORREF kDarkEditor = RGB(31, 31, 31);
constexpr COLORREF kDarkStatus = RGB(43, 43, 43);
constexpr COLORREF kDarkText = RGB(242, 242, 242);
constexpr COLORREF kMutedText = RGB(190, 190, 190);

constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmCaptionColor = 35;
constexpr DWORD kDwmTextColor = 36;

struct EditorSelection {
    DWORD start = 0;
    DWORD end = 0;
};

EditorSelection GetEditSelection(HWND editor) {
    EditorSelection selection;
    SendMessageW(
        editor,
        EM_GETSEL,
        reinterpret_cast<WPARAM>(&selection.start),
        reinterpret_cast<LPARAM>(&selection.end));
    return selection;
}

void SetEditSelection(HWND editor, EditorSelection selection) {
    SendMessageW(editor, EM_SETSEL, selection.start, selection.end);
}

DWORD ClampToDword(size_t value) {
    return value > static_cast<size_t>(MAXDWORD) ? MAXDWORD : static_cast<DWORD>(value);
}

DWORD AdjustIndexForInsert(DWORD index, size_t position, size_t length) {
    const size_t value = static_cast<size_t>(index);
    if (value < position) {
        return index;
    }

    return ClampToDword(value + length);
}

DWORD AdjustIndexForDelete(DWORD index, size_t position, size_t length) {
    const size_t value = static_cast<size_t>(index);
    const size_t deleted_end = position + length;
    if (value <= position) {
        return index;
    }

    if (value <= deleted_end) {
        return ClampToDword(position);
    }

    return ClampToDword(value - length);
}

EditorSelection AdjustSelectionForInsert(EditorSelection selection, size_t position, size_t length) {
    selection.start = AdjustIndexForInsert(selection.start, position, length);
    selection.end = AdjustIndexForInsert(selection.end, position, length);
    return selection;
}

EditorSelection AdjustSelectionForDelete(EditorSelection selection, size_t position, size_t length) {
    selection.start = AdjustIndexForDelete(selection.start, position, length);
    selection.end = AdjustIndexForDelete(selection.end, position, length);
    return selection;
}

EditorSelection ClampSelectionToText(EditorSelection selection, size_t text_length) {
    const DWORD clamped_length = ClampToDword(text_length);
    selection.start = std::min(selection.start, clamped_length);
    selection.end = std::min(selection.end, clamped_length);
    return selection;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::wstring_view TrimLeft(std::wstring_view value) {
    while (!value.empty() && (value.front() == L' ' || value.front() == L'\t')) {
        value.remove_prefix(1);
    }
    return value;
}

std::wstring StripInlineMarkdown(std::wstring_view value) {
    std::wstring output;
    output.reserve(value.size());

    for (size_t index = 0; index < value.size();) {
        if (StartsWith(value.substr(index), L"**") || StartsWith(value.substr(index), L"__")) {
            index += 2;
            continue;
        }

        const wchar_t ch = value[index];
        if (ch == L'`' || ch == L'*' || ch == L'_') {
            ++index;
            continue;
        }

        if (ch == L'[') {
            const size_t close_text = value.find(L']', index + 1);
            if (close_text != std::wstring_view::npos &&
                close_text + 1 < value.size() &&
                value[close_text + 1] == L'(') {
                const size_t close_url = value.find(L')', close_text + 2);
                if (close_url != std::wstring_view::npos) {
                    output.append(value.substr(index + 1, close_text - index - 1));
                    output.append(L" (");
                    output.append(value.substr(close_text + 2, close_url - close_text - 2));
                    output.push_back(L')');
                    index = close_url + 1;
                    continue;
                }
            }
        }

        output.push_back(ch);
        ++index;
    }

    return output;
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

MainWindow::MainWindow(HINSTANCE instance, Config* config, Logger* logger)
    : instance_(instance), config_(config), logger_(logger) {}

bool MainWindow::Create() {
    if (!RegisterWindowClass()) {
        if (logger_ != nullptr) {
            logger_->Error(L"RegisterWindowClass failed");
        }
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        520,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr && logger_ != nullptr) {
        logger_->Error(L"CreateWindowEx failed for main window");
    }

    if (hwnd_ != nullptr) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(
            LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON))));
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(
            instance_,
            MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED)));
        ApplyDarkWindowFrame();
    }

    return hwnd_ != nullptr;
}

void MainWindow::Show(int show_command) {
    ShowWindow(hwnd_, show_command);
    UpdateWindow(hwnd_);
}

bool MainWindow::RegisterWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWindow::WindowProc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = reinterpret_cast<HICON>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR | LR_SHARED));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWindowClassName;

    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool MainWindow::CreateMainMenu() {
    HMENU menu = CreateMenu();
    HMENU file_menu = CreateMenu();
    HMENU edit_menu = CreateMenu();
    HMENU view_menu = CreateMenu();
    HMENU share_menu = CreateMenu();
    HMENU help_menu = CreateMenu();
    if (menu == nullptr || file_menu == nullptr || edit_menu == nullptr ||
        view_menu == nullptr || share_menu == nullptr || help_menu == nullptr) {
        return false;
    }

    AppendMenuW(file_menu, MF_STRING, kFileNewId, L"새 탭(&T)\tCtrl+T");
    AppendMenuW(file_menu, MF_STRING, kFileSaveId, L"저장(&S)\tCtrl+S");
    AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file_menu, MF_STRING, kFileExitId, L"끝내기(&X)");

    AppendMenuW(edit_menu, MF_STRING, kEditUndoId, L"실행 취소(&U)\tCtrl+Z");
    AppendMenuW(edit_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit_menu, MF_STRING, kEditCutId, L"잘라내기(&T)\tCtrl+X");
    AppendMenuW(edit_menu, MF_STRING, kEditCopyId, L"복사(&C)\tCtrl+C");
    AppendMenuW(edit_menu, MF_STRING, kEditPasteId, L"붙여넣기(&P)\tCtrl+V");
    AppendMenuW(edit_menu, MF_STRING, kEditDeleteId, L"삭제(&D)\tDel");
    AppendMenuW(edit_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit_menu, MF_STRING, kEditSelectAllId, L"모두 선택(&A)\tCtrl+A");

    AppendMenuW(view_menu, MF_STRING, kViewMarkdownPreviewId, L"마크다운 미리보기(&M)");

    AppendMenuW(share_menu, MF_STRING, kHostButtonId, L"방 만들기(&H)");
    AppendMenuW(share_menu, MF_STRING, kJoinButtonId, L"방 들어가기(&J)");
    AppendMenuW(share_menu, MF_STRING, kShareCopyCodeId, L"초대 코드 복사(&C)");
    AppendMenuW(share_menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(share_menu, MF_STRING, kShareLeaveRoomId, L"공유 중지(&L)");

    AppendMenuW(help_menu, MF_STRING, kHelpAboutId, L"ShareNotepad 정보(&A)");

    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"파일(&F)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit_menu), L"편집(&E)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view_menu), L"보기(&V)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(share_menu), L"공유(&S)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help_menu), L"도움말(&H)");

    menu_ = menu;
    file_menu_ = file_menu;
    edit_menu_ = edit_menu;
    view_menu_ = view_menu;
    share_menu_ = share_menu;
    return true;
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    MainWindow* window = nullptr;

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<MainWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    } else {
        window = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window != nullptr) {
        return window->HandleMessage(message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT MainWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            if (!CreateMainMenu()) {
                if (logger_ != nullptr) {
                    logger_->Error(L"CreateMainMenu failed");
                }
                return -1;
            }
            if (!CreateControls()) {
                if (logger_ != nullptr) {
                    logger_->Error(L"CreateControls failed");
                }
                return -1;
            }
            server_.SetEventCallback([this] {
                if (hwnd_ != nullptr) {
                    PostMessageW(hwnd_, kSyncEventMessage, 0, 0);
                }
            });
            client_.SetEventCallback([this] {
                if (hwnd_ != nullptr) {
                    PostMessageW(hwnd_, kSyncEventMessage, 0, 0);
                }
            });
            LoadInitialDocument();
            InitializeTabs();
            ShowEditorScreen(L"Idle | 로컬 메모장");
            return 0;

        case WM_TIMER:
            if (wparam == kAutosaveTimerId) {
                KillTimer(hwnd_, kAutosaveTimerId);
                SaveEditorContent();
                return 0;
            }
            break;

        case WM_SIZE:
            Layout();
            return 0;

        case WM_ERASEBKGND: {
            RECT rect{};
            GetClientRect(hwnd_, &rect);
            HBRUSH brush = app_background_brush_ != nullptr
                ? app_background_brush_
                : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            FillRect(reinterpret_cast<HDC>(wparam), &rect, brush);
            return 1;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            HWND control = reinterpret_cast<HWND>(lparam);
            const bool is_tab =
                control == active_tab_label_ ||
                control == inactive_tab_label_ ||
                control == third_tab_label_;
            COLORREF text_color = control == status_label_ ? kMutedText : kDarkText;
            if ((control == active_tab_label_ && active_tab_index_ != 0) ||
                (control == inactive_tab_label_ && active_tab_index_ != 1) ||
                (control == third_tab_label_ && active_tab_index_ != 2)) {
                text_color = kMutedText;
            }
            SetTextColor(hdc, text_color);
            SetBkMode(hdc, is_tab ? OPAQUE : TRANSPARENT);
            if (is_tab) {
                const bool active =
                    (control == active_tab_label_ && active_tab_index_ == 0) ||
                    (control == inactive_tab_label_ && active_tab_index_ == 1) ||
                    (control == third_tab_label_ && active_tab_index_ == 2);
                SetBkColor(hdc, active ? kDarkTab : kInactiveTab);
                return reinterpret_cast<LRESULT>(active ? active_tab_brush_ : inactive_tab_brush_);
            }
            if (control == status_label_) {
                return reinterpret_cast<LRESULT>(status_brush_);
            }
            if (control == tab_bar_ || control == toolbar_bar_) {
                return reinterpret_cast<LRESULT>(toolbar_brush_);
            }
            return reinterpret_cast<LRESULT>(toolbar_brush_);
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kDarkText);
            SetBkColor(hdc, kDarkEditor);
            return reinterpret_cast<LRESULT>(editor_background_brush_);
        }

        case WM_KEYDOWN:
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (wparam == L'A' || wparam == L'a') {
                    SendMessageW(FocusedEditControl(), EM_SETSEL, 0, -1);
                    return 0;
                }

                if (wparam == L'S' || wparam == L's') {
                    dirty_ = true;
                    SaveEditorContent();
                    return 0;
                }

                if (wparam == L'T' || wparam == L't') {
                    HandleNewTab();
                    return 0;
                }
            }

            if (screen_ == Screen::Join && wparam == VK_RETURN) {
                HandleJoinRoom();
                return 0;
            }

            if (screen_ == Screen::Join && wparam == VK_ESCAPE) {
                ShowEditorScreen(CurrentEditorStatusText());
                return 0;
            }
            break;

        case kSyncEventMessage:
            DrainSyncEvents();
            return 0;

        case kReconnectStatusMessage:
            HandleReconnectStatus(wparam, lparam);
            return 0;

        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lparam) == editor_ && HIWORD(wparam) == EN_CHANGE) {
                OnEditorChanged();
                return 0;
            }

            switch (LOWORD(wparam)) {
                case kHostButtonId:
                    HandleCreateRoom();
                    return 0;
                case kJoinButtonId:
                    ShowJoinScreen();
                    return 0;
                case kConnectButtonId:
                    HandleJoinRoom();
                    return 0;
                case kBackButtonId:
                    ShowEditorScreen(CurrentEditorStatusText());
                    return 0;
                case kAdvancedButtonId:
                    ToggleAdvancedJoin();
                    return 0;
                case kFileMenuButtonId:
                    ShowPopupMenu(file_menu_, file_menu_button_);
                    return 0;
                case kEditMenuButtonId:
                    ShowPopupMenu(edit_menu_, edit_menu_button_);
                    return 0;
                case kViewMenuButtonId:
                    ShowPopupMenu(view_menu_, view_menu_button_);
                    return 0;
                case kStyleButtonId:
                    HandleEditCommand(kEditSelectAllId);
                    return 0;
                case kListButtonId:
                    SetWindowTextW(status_label_, L"Markdown | '-' + Space로 목록을 시작하세요.");
                    return 0;
                case kBoldButtonId:
                    SetWindowTextW(status_label_, L"Markdown | '**굵게**' 문법을 사용할 수 있습니다.");
                    return 0;
                case kItalicButtonId:
                    SetWindowTextW(status_label_, L"Markdown | '*기울임*' 문법을 사용할 수 있습니다.");
                    return 0;
                case kStrikeButtonId:
                    SetWindowTextW(status_label_, L"Markdown | '~~취소선~~' 문법은 이후 단계에서 강화합니다.");
                    return 0;
                case kLinkButtonId:
                    SetWindowTextW(status_label_, L"Markdown | [텍스트](주소) 형식으로 링크를 입력하세요.");
                    return 0;
                case kTableButtonId:
                    SetWindowTextW(status_label_, L"Markdown | 표 입력은 이후 단계에서 추가합니다.");
                    return 0;
                case kMarkdownButtonId:
                    ToggleMarkdownPreview();
                    return 0;
                case kShareButtonId:
                    ShowPopupMenu(share_menu_, share_button_);
                    return 0;
                case kAccountButtonId:
                    ShowAboutDialog();
                    return 0;
                case kSettingsButtonId:
                    ShowAboutDialog();
                    return 0;
                case kTab0ButtonId:
                    SwitchToTab(0);
                    return 0;
                case kTab1ButtonId:
                    SwitchToTab(1);
                    return 0;
                case kTab2ButtonId:
                    SwitchToTab(2);
                    return 0;
                case kNewTabButtonId:
                    HandleNewTab();
                    return 0;
                case kFileNewId:
                    HandleNewTab();
                    return 0;
                case kFileSaveId:
                    dirty_ = true;
                    SaveEditorContent();
                    return 0;
                case kFileExitId:
                    SendMessageW(hwnd_, WM_CLOSE, 0, 0);
                    return 0;
                case kEditUndoId:
                case kEditCutId:
                case kEditCopyId:
                case kEditPasteId:
                case kEditDeleteId:
                case kEditSelectAllId:
                    HandleEditCommand(LOWORD(wparam));
                    return 0;
                case kViewMarkdownPreviewId:
                    ToggleMarkdownPreview();
                    return 0;
                case kShareCopyCodeId:
                    CopyInviteCode();
                    return 0;
                case kShareLeaveRoomId:
                    HandleLeaveRoom();
                    return 0;
                case kHelpAboutId:
                    ShowAboutDialog();
                    return 0;
                default:
                    break;
            }
            break;

        case WM_IME_STARTCOMPOSITION:
            ime_composing_ = true;
            return DefWindowProcW(hwnd_, message, wparam, lparam);

        case WM_IME_COMPOSITION:
            return DefWindowProcW(hwnd_, message, wparam, lparam);

        case WM_IME_ENDCOMPOSITION:
            ime_composing_ = false;
            ProcessEditorChange();
            return DefWindowProcW(hwnd_, message, wparam, lparam);

        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            HWND target = WindowFromPoint(point);
            if (target == editor_ || target == markdown_preview_) {
                SendMessageW(target, message, wparam, lparam);
                return 0;
            }
            if (screen_ == Screen::Editor && editor_ != nullptr) {
                SendMessageW(editor_, message, wparam, lparam);
                return 0;
            }
            break;
        }

        case WM_SETFOCUS:
            if (screen_ == Screen::Editor && editor_ != nullptr) {
                SetFocus(editor_);
            }
            return 0;

        case WM_CLOSE:
            SaveEditorContent();
            StopReconnectLoop();
            discovery_announcer_.Stop();
            client_.Disconnect();
            server_.Stop();
            DestroyWindow(hwnd_);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

bool MainWindow::CreateControls() {
    app_background_brush_ = CreateSolidBrush(kDarkBackground);
    toolbar_brush_ = CreateSolidBrush(kDarkToolbar);
    active_tab_brush_ = CreateSolidBrush(kDarkTab);
    inactive_tab_brush_ = CreateSolidBrush(kInactiveTab);
    editor_background_brush_ = CreateSolidBrush(kDarkEditor);
    status_brush_ = CreateSolidBrush(kDarkStatus);
    ui_font_ = CreateFontW(
        -16,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");
    toolbar_font_ = CreateFontW(
        -18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");
    editor_font_ = CreateFontW(
        -18,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Consolas");

    tab_bar_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    active_tab_label_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"  current.txt",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTab0ButtonId)),
        instance_,
        nullptr);

    inactive_tab_label_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"  새 탭 1",
        WS_CHILD | SS_NOTIFY | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTab1ButtonId)),
        instance_,
        nullptr);

    third_tab_label_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"STATIC",
        L"  새 탭 2",
        WS_CHILD | SS_NOTIFY | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTab2ButtonId)),
        instance_,
        nullptr);

    new_tab_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"+",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNewTabButtonId)),
        instance_,
        nullptr);

    toolbar_bar_ = CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    file_menu_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"파일",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFileMenuButtonId)),
        instance_,
        nullptr);

    edit_menu_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"편집",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditMenuButtonId)),
        instance_,
        nullptr);

    view_menu_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"보기",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kViewMenuButtonId)),
        instance_,
        nullptr);

    style_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"제목",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStyleButtonId)),
        instance_,
        nullptr);

    list_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"목록",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListButtonId)),
        instance_,
        nullptr);

    bold_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"굵게",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoldButtonId)),
        instance_,
        nullptr);

    italic_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"기울임",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kItalicButtonId)),
        instance_,
        nullptr);

    strike_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"취소선",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStrikeButtonId)),
        instance_,
        nullptr);

    link_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"링크",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLinkButtonId)),
        instance_,
        nullptr);

    table_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"표",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTableButtonId)),
        instance_,
        nullptr);

    markdown_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"MD 보기",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkdownButtonId)),
        instance_,
        nullptr);

    share_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"공유⌄",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShareButtonId)),
        instance_,
        nullptr);

    account_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"정보",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAccountButtonId)),
        instance_,
        nullptr);

    settings_button_ = CreateWindowExW(
        0,
        L"STATIC",
        L"설정",
        WS_CHILD | WS_VISIBLE | SS_NOTIFY | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettingsButtonId)),
        instance_,
        nullptr);

    title_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"ShareNotepad",
        WS_CHILD | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    host_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"방 만들기",
        WS_CHILD | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHostButtonId)),
        instance_,
        nullptr);

    join_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"방 들어가기",
        WS_CHILD | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kJoinButtonId)),
        instance_,
        nullptr);

    recent_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"최근 문서: current.txt",
        WS_CHILD | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_title_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"방 들어가기",
        WS_CHILD | SS_CENTER,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_code_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"초대 코드",
        WS_CHILD | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_code_edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_host_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"연결 주소",
        WS_CHILD | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_host_edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"127.0.0.1",
        WS_CHILD | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_port_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"포트",
        WS_CHILD | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    join_port_edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"7777",
        WS_CHILD | ES_AUTOHSCROLL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    connect_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"연결",
        WS_CHILD | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kConnectButtonId)),
        instance_,
        nullptr);

    back_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"뒤로",
        WS_CHILD | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackButtonId)),
        instance_,
        nullptr);

    advanced_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"고급 연결",
        WS_CHILD | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAdvancedButtonId)),
        instance_,
        nullptr);

    static HMODULE rich_edit_module = LoadLibraryW(L"Msftedit.dll");
    const wchar_t* editor_class = rich_edit_module != nullptr ? MSFTEDIT_CLASS : L"EDIT";

    editor_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        editor_class,
        L"",
        WS_CHILD | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    markdown_preview_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    status_label_ = CreateWindowExW(
        0,
        L"STATIC",
        L"준비됨",
        WS_CHILD | SS_LEFT,
        0,
        0,
        0,
        0,
        hwnd_,
        nullptr,
        instance_,
        nullptr);

    HWND controls[] = {
        tab_bar_,
        active_tab_label_,
        inactive_tab_label_,
        third_tab_label_,
        new_tab_button_,
        toolbar_bar_,
        file_menu_button_,
        edit_menu_button_,
        view_menu_button_,
        style_button_,
        list_button_,
        bold_button_,
        italic_button_,
        strike_button_,
        link_button_,
        table_button_,
        markdown_button_,
        share_button_,
        account_button_,
        settings_button_,
        title_label_,
        host_button_,
        join_button_,
        recent_label_,
        join_title_label_,
        join_code_label_,
        join_code_edit_,
        join_host_label_,
        join_host_edit_,
        join_port_label_,
        join_port_edit_,
        connect_button_,
        back_button_,
        advanced_button_,
        editor_,
        markdown_preview_,
        status_label_};
    for (HWND control : controls) {
        if (control == nullptr) {
            return false;
        }
        ApplyDefaultFont(control);
    }

    if (rich_edit_module != nullptr) {
        SendMessageW(editor_, EM_SETEVENTMASK, 0, ENM_CHANGE);
        SendMessageW(editor_, EM_SETEDITSTYLE, SES_EXTENDBACKCOLOR, SES_EXTENDBACKCOLOR);
        ResetRichEditInsertionFormat();
    }
    ApplyDarkEditorTheme();

    return true;
}

void MainWindow::ApplyDefaultFont(HWND control) {
    HFONT font = ui_font_ != nullptr ? ui_font_ : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (control == editor_ || control == markdown_preview_) {
        font = editor_font_ != nullptr ? editor_font_ : font;
    } else if (
        control == active_tab_label_ ||
        control == inactive_tab_label_ ||
        control == third_tab_label_ ||
        control == new_tab_button_ ||
        control == file_menu_button_ ||
        control == edit_menu_button_ ||
        control == view_menu_button_ ||
        control == style_button_ ||
        control == list_button_ ||
        control == bold_button_ ||
        control == italic_button_ ||
        control == strike_button_ ||
        control == link_button_ ||
        control == table_button_ ||
        control == markdown_button_ ||
        control == share_button_ ||
        control == account_button_ ||
        control == settings_button_) {
        font = toolbar_font_ != nullptr ? toolbar_font_ : font;
    }
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void MainWindow::ApplyDarkWindowFrame() {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    COLORREF caption = kDarkBackground;
    COLORREF text = kDarkText;
    DwmSetWindowAttribute(hwnd_, kDwmCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, kDwmTextColor, &text, sizeof(text));
}

void MainWindow::ApplyDarkEditorTheme() {
    SetWindowTheme(editor_, L"DarkMode_Explorer", nullptr);
    SetWindowTheme(markdown_preview_, L"DarkMode_Explorer", nullptr);
    SendMessageW(editor_, EM_SETBKGNDCOLOR, 0, kDarkEditor);

    CHARFORMAT2W editor_format{};
    editor_format.cbSize = sizeof(editor_format);
    editor_format.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    editor_format.crTextColor = kDarkText;
    editor_format.yHeight = kDefaultTextHeight;
    wcsncpy_s(editor_format.szFaceName, L"Consolas", _TRUNCATE);
    SendMessageW(editor_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&editor_format));

    SendMessageW(markdown_preview_, EM_SETBKGNDCOLOR, 0, kDarkEditor);
}

void MainWindow::LoadInitialDocument() {
    std::wstring content;
    suppress_editor_change_ = true;
    if (storage_.LoadCurrent(&content)) {
        SetWindowTextW(editor_, content.c_str());
        last_known_text_ = content;
        if (logger_ != nullptr) {
            logger_->Info(L"Loaded current document");
        }
    } else {
        SetWindowTextW(editor_, L"");
        last_known_text_.clear();
        if (logger_ != nullptr) {
            logger_->Error(storage_.last_error());
        }
    }
    suppress_editor_change_ = false;
    dirty_ = false;
}

void MainWindow::InitializeTabs() {
    tabs_.clear();
    tabs_.push_back(TabState{L"current.txt", last_known_text_, false});
    active_tab_index_ = 0;
    UpdateTabBar();
}

void MainWindow::SyncActiveTabFromEditor() {
    if (active_tab_index_ >= tabs_.size()) {
        return;
    }

    tabs_[active_tab_index_].content = GetEditorText();
    tabs_[active_tab_index_].dirty = dirty_;
}

void MainWindow::SwitchToTab(size_t index) {
    if (index >= tabs_.size()) {
        return;
    }

    if (sync_role_ != SyncRole::Offline) {
        SetWindowTextW(status_label_, L"공유 중에는 탭 전환을 지원하지 않습니다. 공유를 중지한 뒤 전환하세요.");
        return;
    }

    SyncActiveTabFromEditor();
    active_tab_index_ = index;

    suppress_editor_change_ = true;
    SetWindowTextW(editor_, tabs_[active_tab_index_].content.c_str());
    suppress_editor_change_ = false;

    last_known_text_ = tabs_[active_tab_index_].content;
    dirty_ = tabs_[active_tab_index_].dirty;
    current_revision_ = 0;
    ResetRichEditInsertionFormat();
    ResetRichEditParagraphFormat();
    UpdateMarkdownPreview();
    UpdateTabBar();
    SetWindowTextW(status_label_, (L"탭 전환 | " + tabs_[active_tab_index_].title).c_str());
    SetFocus(editor_);
}

void MainWindow::HandleNewTab() {
    if (sync_role_ != SyncRole::Offline) {
        SetWindowTextW(status_label_, L"공유 중에는 새 탭을 만들 수 없습니다. 공유를 중지한 뒤 추가하세요.");
        return;
    }

    if (tabs_.size() >= kMaxTabs) {
        SetWindowTextW(status_label_, L"MVP에서는 탭을 3개까지 지원합니다.");
        return;
    }

    SyncActiveTabFromEditor();
    const size_t new_index = tabs_.size();
    std::wostringstream title;
    title << L"새 탭 " << new_index;
    tabs_.push_back(TabState{title.str(), L"", false});
    SwitchToTab(new_index);
}

void MainWindow::UpdateTabBar() {
    HWND tab_controls[] = {active_tab_label_, inactive_tab_label_, third_tab_label_};
    constexpr size_t tab_control_count = sizeof(tab_controls) / sizeof(tab_controls[0]);
    for (size_t index = 0; index < tab_control_count; ++index) {
        const bool visible = index < tabs_.size();
        SetChildVisible(tab_controls[index], visible);
        if (visible) {
            const std::wstring text = TabTitleText(index);
            SetWindowTextW(tab_controls[index], text.c_str());
        }
    }

    SetChildVisible(new_tab_button_, tabs_.size() < kMaxTabs);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

std::wstring MainWindow::TabTitleText(size_t index) const {
    if (index >= tabs_.size()) {
        return {};
    }

    std::wstring title = tabs_[index].title;
    const int available_width = std::max(24, tab_label_width_ - 18);
    size_t max_title_chars = static_cast<size_t>(std::max(1, available_width / 9));
    if (tabs_[index].dirty && max_title_chars > 2) {
        max_title_chars -= 2;
    }

    if (title.size() > max_title_chars) {
        if (max_title_chars <= 3) {
            title = std::to_wstring(index + 1);
        } else {
            title = title.substr(0, max_title_chars - 3) + L"...";
        }
    }

    std::wstring text = L" ";
    text += title;
    if (tabs_[index].dirty) {
        text += L" *";
    }
    return text;
}

void MainWindow::Layout() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;

    MoveWindow(tab_bar_, 0, 0, width, kTabBarHeight, TRUE);
    const int visible_tab_count = static_cast<int>(std::max<size_t>(1, tabs_.size()));
    const int new_tab_width = tabs_.size() < kMaxTabs ? 36 : 0;
    const int tab_gap = 10;
    const int tab_area_width = std::max(0, width - 24 - new_tab_width - tab_gap * visible_tab_count);
    const int tab_width = std::max(54, std::min(190, tab_area_width / visible_tab_count));
    if (tab_label_width_ != tab_width) {
        tab_label_width_ = tab_width;
        UpdateTabBar();
    }
    int tab_x = 12;
    MoveWindow(active_tab_label_, tab_x, 6, tab_width, 30, TRUE);
    tab_x += tab_width + 10;
    MoveWindow(inactive_tab_label_, tab_x, 6, tab_width, 30, TRUE);
    tab_x += tab_width + 10;
    MoveWindow(third_tab_label_, tab_x, 6, tab_width, 30, TRUE);
    tab_x += tab_width + 12;
    MoveWindow(new_tab_button_, tab_x, 7, 36, 28, TRUE);
    MoveWindow(toolbar_bar_, 0, kTabBarHeight, width, kToolbarHeight, TRUE);

    int x = 10;
    const int toolbar_y = kTabBarHeight + 8;
    MoveWindow(file_menu_button_, x, toolbar_y, 48, 28, TRUE);
    x += 68;
    MoveWindow(edit_menu_button_, x, toolbar_y, 48, 28, TRUE);
    x += 68;
    MoveWindow(view_menu_button_, x, toolbar_y, 48, 28, TRUE);
    const int menu_end_x = x + 48;

    const bool show_share = width >= 360;
    const int share_width = width >= 560 ? 78 : 58;
    const int share_x = std::max(menu_end_x + 12, width - share_width - 12);
    SetWindowTextW(share_button_, width >= 560 ? L"공유⌄" : L"공유");
    ShowWindow(share_button_, show_share ? SW_SHOW : SW_HIDE);
    if (show_share) {
        MoveWindow(share_button_, share_x, toolbar_y, share_width, 28, TRUE);
    }

    int command_x = menu_end_x + 28;
    const int command_limit = (show_share ? share_x : width - 12) - 12;
    auto place_toolbar_button = [&](HWND control, int control_width) {
        const bool fits = command_x + control_width <= command_limit;
        ShowWindow(control, fits ? SW_SHOW : SW_HIDE);
        if (fits) {
            MoveWindow(control, command_x, toolbar_y, control_width, 28, TRUE);
            command_x += control_width + 12;
        }
        return fits;
    };

    place_toolbar_button(style_button_, 58);
    place_toolbar_button(list_button_, 58);
    place_toolbar_button(bold_button_, 58);
    place_toolbar_button(italic_button_, 66);
    place_toolbar_button(link_button_, 58);
    place_toolbar_button(markdown_button_, 82);

    ShowWindow(strike_button_, SW_HIDE);
    ShowWindow(table_button_, SW_HIDE);
    ShowWindow(account_button_, SW_HIDE);
    ShowWindow(settings_button_, SW_HIDE);

    if (screen_ == Screen::Start) {
        const int center_x = width / 2;
        const int start_y = std::max(kTopChromeHeight + kMargin, height / 2 - 70);

        MoveWindow(title_label_, kMargin, kMargin, width - kMargin * 2, 32, TRUE);
        MoveWindow(host_button_, center_x - kButtonWidth / 2, start_y, kButtonWidth, kButtonHeight, TRUE);
        MoveWindow(join_button_, center_x - kButtonWidth / 2, start_y + 46, kButtonWidth, kButtonHeight, TRUE);
        MoveWindow(recent_label_, kMargin, start_y + 102, width - kMargin * 2, 24, TRUE);
        MoveWindow(status_label_, kMargin, height - kStatusHeight, width - kMargin * 2, kStatusHeight, TRUE);
        return;
    }

    if (screen_ == Screen::Join) {
        const int center_x = width / 2;
        const int form_x = center_x - (kLabelWidth + kInputWidth) / 2;
        const int start_y = std::max(kTopChromeHeight + kMargin, height / 2 - (advanced_join_visible_ ? 112 : 72));

        MoveWindow(join_title_label_, kMargin, kTopChromeHeight + kMargin, width - kMargin * 2, 32, TRUE);
        MoveWindow(join_code_label_, form_x, start_y, kLabelWidth, kInputHeight, TRUE);
        MoveWindow(join_code_edit_, form_x + kLabelWidth, start_y, kInputWidth, kInputHeight, TRUE);
        if (advanced_join_visible_) {
            MoveWindow(join_host_label_, form_x, start_y + 38, kLabelWidth, kInputHeight, TRUE);
            MoveWindow(join_host_edit_, form_x + kLabelWidth, start_y + 38, kInputWidth, kInputHeight, TRUE);
            MoveWindow(join_port_label_, form_x, start_y + 76, kLabelWidth, kInputHeight, TRUE);
            MoveWindow(join_port_edit_, form_x + kLabelWidth, start_y + 76, kInputWidth, kInputHeight, TRUE);
        }
        const int button_y = advanced_join_visible_ ? start_y + 122 : start_y + 44;
        MoveWindow(connect_button_, center_x - kButtonWidth - 6, button_y, kButtonWidth, kButtonHeight, TRUE);
        MoveWindow(back_button_, center_x + 6, button_y, kButtonWidth, kButtonHeight, TRUE);
        MoveWindow(advanced_button_, center_x - kButtonWidth / 2, button_y + 44, kButtonWidth, kButtonHeight, TRUE);
        MoveWindow(status_label_, kMargin, height - kStatusHeight, width - kMargin * 2, kStatusHeight, TRUE);
        return;
    }

    const int editor_y = kTopChromeHeight;
    const int editor_height = std::max(0, height - kTopChromeHeight - kStatusHeight);
    if (markdown_preview_visible_) {
        const int gap = 6;
        const int editor_width = std::max(0, (width - gap) / 2);
        MoveWindow(editor_, 0, editor_y, editor_width, editor_height, TRUE);
        MoveWindow(markdown_preview_, editor_width + gap, editor_y, std::max(0, width - editor_width - gap), editor_height, TRUE);
    } else {
        MoveWindow(editor_, 0, editor_y, width, editor_height, TRUE);
        MoveWindow(markdown_preview_, width, editor_y, 0, editor_height, TRUE);
    }
    MoveWindow(status_label_, 4, height - kStatusHeight, std::max(0, width - 8), kStatusHeight, TRUE);
}

void MainWindow::ShowStartScreen() {
    screen_ = Screen::Start;
    sync_role_ = SyncRole::Offline;
    SetChildVisible(title_label_, true);
    SetChildVisible(host_button_, true);
    SetChildVisible(join_button_, true);
    SetChildVisible(recent_label_, true);
    HideJoinControls();
    SetChildVisible(editor_, false);
    SetChildVisible(markdown_preview_, false);
    SetChildVisible(status_label_, true);
    SetWindowTextW(status_label_, L"Idle");
    Layout();
    UpdateMenuState();
}

void MainWindow::ShowJoinScreen() {
    screen_ = Screen::Join;
    advanced_join_visible_ = false;
    SetChildVisible(title_label_, false);
    SetChildVisible(host_button_, false);
    SetChildVisible(join_button_, false);
    SetChildVisible(recent_label_, false);
    SetJoinControlsVisible(true);
    SetChildVisible(editor_, false);
    SetChildVisible(markdown_preview_, false);
    SetChildVisible(status_label_, true);

    const uint16_t port = config_ != nullptr ? config_->port() : 7777;
    SetWindowTextW(join_port_edit_, std::to_wstring(port).c_str());
    SetWindowTextW(status_label_, L"Idle | 초대 코드를 입력하세요.");
    Layout();
    UpdateMenuState();
    SetFocus(join_code_edit_);
}

void MainWindow::ShowEditorScreen(std::wstring_view status_text) {
    screen_ = Screen::Editor;
    std::wstring status(status_text);
    SetWindowTextW(status_label_, status.c_str());
    SetChildVisible(title_label_, false);
    SetChildVisible(host_button_, false);
    SetChildVisible(join_button_, false);
    SetChildVisible(recent_label_, false);
    HideJoinControls();
    SetChildVisible(editor_, true);
    SetChildVisible(markdown_preview_, markdown_preview_visible_);
    SetChildVisible(status_label_, true);
    UpdateMarkdownPreview();
    UpdateTabBar();
    Layout();
    UpdateMenuState();
    SetFocus(editor_);
}

void MainWindow::SetChildVisible(HWND control, bool visible) {
    ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

void MainWindow::OnEditorChanged() {
    if (suppress_editor_change_ || remote_applying_ || ime_composing_ || markdown_shortcut_applying_) {
        return;
    }

    ResetMarkdownStyleAtLineBreakIfNeeded();
    ApplyMarkdownShortcutIfNeeded();
    ProcessEditorChange();
}

void MainWindow::ProcessEditorChange() {
    const std::wstring current_text = GetEditorText();
    const EditorDiffResult diff = ComputeEditorDiff(last_known_text_, current_text);
    if (!diff.valid) {
        if (diff.document_too_large) {
            SetWindowTextW(status_label_, L"문서 크기 제한을 초과했습니다.");
            if (logger_ != nullptr) {
                logger_->Warning(L"Editor change rejected: document too large");
            }
            suppress_editor_change_ = true;
            SetWindowTextW(editor_, last_known_text_.c_str());
            suppress_editor_change_ = false;
            return;
        }

        if (logger_ != nullptr) {
            logger_->Warning(L"Editor diff failed");
        }
        return;
    }

    if (!diff.changed) {
        return;
    }

    if (diff.changed && logger_ != nullptr) {
        std::wostringstream message;
        message << L"Editor changed operations=" << diff.operations.size();
        logger_->Debug(message.str());
    }

    SendLocalOperations(diff);
    last_known_text_ = current_text;
    dirty_ = true;
    if (active_tab_index_ < tabs_.size()) {
        tabs_[active_tab_index_].content = current_text;
        tabs_[active_tab_index_].dirty = true;
        UpdateTabBar();
    }
    UpdateMarkdownPreview();
    if (screen_ == Screen::Editor) {
        SetWindowTextW(status_label_, L"편집 중 | 자동저장 대기");
    }
    SetTimer(hwnd_, kAutosaveTimerId, kAutosaveDelayMs, nullptr);
}

void MainWindow::SendLocalOperations(const EditorDiffResult& diff) {
    if (sync_role_ == SyncRole::Offline) {
        return;
    }

    for (const EditorOperation& operation : diff.operations) {
        if (sync_role_ == SyncRole::Host) {
            ProtocolMessage apply;
            if (!server_.ApplyLocalOperation(operation, local_client_id_, &apply)) {
                if (logger_ != nullptr) {
                    logger_->Warning(L"Local host operation rejected: " + server_.last_error());
                }
                RequestFullSync();
                continue;
            }

            uint64_t revision = current_revision_;
            if (apply.GetNumber(L"rev", &revision)) {
                current_revision_ = revision;
            }
            continue;
        }

        if (sync_role_ == SyncRole::Guest) {
            const uint64_t base_revision = current_revision_;
            bool sent = false;
            if (operation.type == EditorOperationType::Insert) {
                sent = client_.SendInsert(base_revision, operation.position, operation.text);
            } else {
                sent = client_.SendDelete(base_revision, operation.position, operation.length);
            }

            if (!sent && logger_ != nullptr) {
                logger_->Warning(L"Client operation send failed: " + client_.last_error());
            }
        }
    }
}

void MainWindow::DrainSyncEvents() {
    SyncEvent event;
    while (server_.TryPopEvent(&event)) {
        HandleSyncEvent(event);
    }

    while (client_.TryPopEvent(&event)) {
        HandleSyncEvent(event);
    }
}

void MainWindow::HandleSyncEvent(const SyncEvent& event) {
    switch (event.type) {
        case SyncEventType::Apply:
            ApplySyncApply(event.message);
            return;
        case SyncEventType::SyncResponse:
            ApplySyncResponse(event.message);
            return;
        case SyncEventType::Disconnected:
            if (sync_role_ == SyncRole::Guest) {
                StartReconnectLoop();
            } else {
                SetWindowTextW(status_label_, L"Disconnected | 상대 연결이 끊겼습니다.");
            }
            return;
        case SyncEventType::Error:
            if (!event.detail.empty()) {
                const std::wstring message = FriendlySyncErrorMessage(event.detail);
                SetWindowTextW(status_label_, message.c_str());
            }
            return;
        default:
            return;
    }
}

void MainWindow::StartReconnectLoop() {
    if (sync_role_ != SyncRole::Guest || reconnect_host_.empty() || reconnect_port_ == 0) {
        SetWindowTextW(status_label_, L"Disconnected | 재연결 정보가 없습니다.");
        return;
    }

    if (reconnecting_.exchange(true)) {
        return;
    }

    reconnect_stop_ = false;
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }

    SetWindowTextW(status_label_, L"Reconnecting | 재연결 중");
    reconnect_thread_ = std::thread(&MainWindow::ReconnectLoop, this);
}

void MainWindow::StopReconnectLoop() {
    reconnect_stop_ = true;
    if (reconnect_thread_.joinable() && reconnect_thread_.get_id() != std::this_thread::get_id()) {
        reconnect_thread_.join();
    }
    reconnecting_ = false;
}

void MainWindow::ReconnectLoop() {
    for (int attempt = 1; attempt <= kMaxReconnectAttempts && !reconnect_stop_; ++attempt) {
        PostMessageW(hwnd_, kReconnectStatusMessage, static_cast<WPARAM>(attempt), 0);

        if (client_.Connect(
                reconnect_host_,
                reconnect_port_,
                session_code_,
                reconnect_device_id_,
                reconnect_nickname_)) {
            reconnecting_ = false;
            PostMessageW(hwnd_, kReconnectStatusMessage, 0, 1);
            return;
        }

        const int delay_ms = std::min(8000, 1000 << std::min(attempt - 1, 3));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        while (!reconnect_stop_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    reconnecting_ = false;
    if (!reconnect_stop_) {
        PostMessageW(hwnd_, kReconnectStatusMessage, 0, 2);
    }
}

void MainWindow::HandleReconnectStatus(WPARAM wparam, LPARAM lparam) {
    if (lparam == 1) {
        SetWindowTextW(status_label_, L"Connected | 재연결됨");
        return;
    }

    if (lparam == 2) {
        SetWindowTextW(status_label_, L"Disconnected | 재연결 실패");
        return;
    }

    std::wostringstream status;
    status << L"Reconnecting | 재연결 "
           << static_cast<unsigned int>(wparam)
           << L"/"
           << kMaxReconnectAttempts;
    SetWindowTextW(status_label_, status.str().c_str());
}

void MainWindow::ApplySyncResponse(const ProtocolMessage& message) {
    std::wstring text;
    uint64_t revision = 0;
    if (!message.GetString(L"text", &text) || !message.GetNumber(L"rev", &revision)) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid SYNC_RESPONSE");
        }
        return;
    }

    uint64_t chunk_index = 0;
    uint64_t chunk_count = 1;
    message.GetNumber(L"chunk_index", &chunk_index);
    message.GetNumber(L"chunk_count", &chunk_count);
    if (chunk_count == 0 || chunk_index >= chunk_count) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid SYNC_RESPONSE chunk");
        }
        return;
    }

    if (chunk_count > 1) {
        if (sync_response_revision_ != revision || sync_response_chunks_.size() != chunk_count) {
            sync_response_revision_ = revision;
            sync_response_chunks_.assign(static_cast<size_t>(chunk_count), std::wstring());
            sync_response_received_.assign(static_cast<size_t>(chunk_count), false);
        }

        sync_response_chunks_[static_cast<size_t>(chunk_index)] = std::move(text);
        sync_response_received_[static_cast<size_t>(chunk_index)] = true;
        const bool complete = std::all_of(
            sync_response_received_.begin(),
            sync_response_received_.end(),
            [](bool received) { return received; });
        if (!complete) {
            SetWindowTextW(status_label_, L"Syncing | 전체 문서 동기화 중");
            return;
        }

        std::wstring merged;
        for (const std::wstring& chunk : sync_response_chunks_) {
            merged += chunk;
        }
        sync_response_chunks_.clear();
        sync_response_received_.clear();
        ApplyFullSyncDocument(std::move(merged), revision);
        return;
    }

    sync_response_chunks_.clear();
    sync_response_received_.clear();
    ApplyFullSyncDocument(std::move(text), revision);
}

void MainWindow::ApplyFullSyncDocument(std::wstring text, uint64_t revision) {
    EditorSelection selection = ClampSelectionToText(GetEditSelection(editor_), text.size());
    remote_applying_ = true;
    suppress_editor_change_ = true;
    SetWindowTextW(editor_, text.c_str());
    SetEditSelection(editor_, selection);
    suppress_editor_change_ = false;
    remote_applying_ = false;

    last_known_text_ = std::move(text);
    current_revision_ = revision;
    dirty_ = true;
    if (active_tab_index_ < tabs_.size()) {
        tabs_[active_tab_index_].content = last_known_text_;
        tabs_[active_tab_index_].dirty = true;
        UpdateTabBar();
    }
    UpdateMarkdownPreview();
    SetTimer(hwnd_, kAutosaveTimerId, kAutosaveDelayMs, nullptr);
    SetWindowTextW(status_label_, L"Connected | 동기화 완료");
}

void MainWindow::ApplySyncApply(const ProtocolMessage& message) {
    uint64_t revision = 0;
    uint64_t position = 0;
    std::wstring operation;
    if (!message.GetNumber(L"rev", &revision) ||
        !message.GetNumber(L"pos", &position) ||
        !message.GetString(L"op", &operation)) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid APPLY");
        }
        return;
    }

    if (revision <= current_revision_) {
        return;
    }

    if (message.client == local_client_id_) {
        current_revision_ = revision;
        return;
    }

    if (revision != current_revision_ + 1) {
        RequestFullSync();
        return;
    }

    std::wstring text = GetEditorText();
    EditorSelection adjusted_selection = GetEditSelection(editor_);
    DocumentApplyStatus status = DocumentApplyStatus::InvalidOperation;
    if (operation == L"INSERT") {
        std::wstring inserted;
        if (!message.GetString(L"text", &inserted)) {
            status = DocumentApplyStatus::InvalidOperation;
        } else {
            status = ApplyInsertToContent(&text, static_cast<size_t>(position), inserted);
            if (status == DocumentApplyStatus::Applied) {
                adjusted_selection = AdjustSelectionForInsert(
                    adjusted_selection,
                    static_cast<size_t>(position),
                    inserted.size());
            }
        }
    } else if (operation == L"DELETE") {
        uint64_t length = 0;
        if (!message.GetNumber(L"len", &length)) {
            status = DocumentApplyStatus::InvalidOperation;
        } else {
            status = ApplyDeleteToContent(&text, static_cast<size_t>(position), static_cast<size_t>(length));
            if (status == DocumentApplyStatus::Applied) {
                adjusted_selection = AdjustSelectionForDelete(
                    adjusted_selection,
                    static_cast<size_t>(position),
                    static_cast<size_t>(length));
            }
        }
    }

    if (status != DocumentApplyStatus::Applied) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Remote APPLY failed: " + DocumentApplyStatusToString(status));
        }
        RequestFullSync();
        return;
    }

    remote_applying_ = true;
    suppress_editor_change_ = true;
    SetWindowTextW(editor_, text.c_str());
    SetEditSelection(editor_, ClampSelectionToText(adjusted_selection, text.size()));
    suppress_editor_change_ = false;
    remote_applying_ = false;

    last_known_text_ = std::move(text);
    current_revision_ = revision;
    dirty_ = true;
    if (active_tab_index_ < tabs_.size()) {
        tabs_[active_tab_index_].content = last_known_text_;
        tabs_[active_tab_index_].dirty = true;
        UpdateTabBar();
    }
    UpdateMarkdownPreview();
    SetTimer(hwnd_, kAutosaveTimerId, kAutosaveDelayMs, nullptr);
    SetWindowTextW(status_label_, L"Connected | 동기화 완료");
}

void MainWindow::RequestFullSync() {
    if (sync_role_ == SyncRole::Guest) {
        client_.SendSyncRequest(current_revision_);
        SetWindowTextW(status_label_, L"Syncing | 전체 동기화 요청");
        return;
    }

    if (sync_role_ == SyncRole::Host) {
        std::wstring text = server_.document();
        current_revision_ = server_.revision();
        EditorSelection selection = ClampSelectionToText(GetEditSelection(editor_), text.size());
        remote_applying_ = true;
        suppress_editor_change_ = true;
        SetWindowTextW(editor_, text.c_str());
        SetEditSelection(editor_, selection);
        suppress_editor_change_ = false;
        remote_applying_ = false;
        last_known_text_ = std::move(text);
        dirty_ = true;
        if (active_tab_index_ < tabs_.size()) {
            tabs_[active_tab_index_].content = last_known_text_;
            tabs_[active_tab_index_].dirty = true;
            UpdateTabBar();
        }
        UpdateMarkdownPreview();
        SetTimer(hwnd_, kAutosaveTimerId, kAutosaveDelayMs, nullptr);
        SetWindowTextW(status_label_, L"Connected | 재동기화 완료");
    }
}

void MainWindow::SaveEditorContent() {
    if (!dirty_) {
        return;
    }

    if (active_tab_index_ > 0) {
        SyncActiveTabFromEditor();
        if (screen_ == Screen::Editor) {
            SetWindowTextW(status_label_, L"새 탭은 현재 실행 중에만 보관됩니다. 파일 저장은 이후 단계에서 추가합니다.");
        }
        return;
    }

    const std::wstring text = GetEditorText();
    if (storage_.SaveCurrent(text)) {
        dirty_ = false;
        if (!tabs_.empty()) {
            tabs_[0].content = text;
            tabs_[0].dirty = false;
            UpdateTabBar();
        }
        if (screen_ == Screen::Editor) {
            SetWindowTextW(status_label_, L"로컬 저장됨");
        }
        if (logger_ != nullptr) {
            logger_->Info(L"Saved current document");
        }
        return;
    }

    if (screen_ == Screen::Editor) {
        SetWindowTextW(status_label_, L"자동저장 실패");
    }
    if (logger_ != nullptr) {
        logger_->Error(storage_.last_error());
    }
}

std::wstring MainWindow::GetEditorText() const {
    const int length = GetWindowTextLengthW(editor_);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(editor_, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(0, copied)));
    return text;
}

std::wstring MainWindow::GetControlText(HWND control) const {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return {};
    }

    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(std::max(0, copied)));
    return text;
}

bool MainWindow::IsOwnedEditControl(HWND control) const {
    return control == editor_ ||
           control == markdown_preview_ ||
           control == join_code_edit_ ||
           control == join_host_edit_ ||
           control == join_port_edit_;
}

HWND MainWindow::FocusedEditControl() const {
    HWND focus = GetFocus();
    if (IsOwnedEditControl(focus)) {
        return focus;
    }

    return editor_;
}

void MainWindow::HandleNewDocument() {
    HandleLeaveRoom();

    suppress_editor_change_ = true;
    SetWindowTextW(editor_, L"");
    suppress_editor_change_ = false;

    last_known_text_.clear();
    current_revision_ = 0;
    dirty_ = true;
    UpdateMarkdownPreview();
    SetTimer(hwnd_, kAutosaveTimerId, kAutosaveDelayMs, nullptr);
    ShowEditorScreen(L"Idle | 새 문서");
}

void MainWindow::HandleEditCommand(int command_id) {
    HWND target = FocusedEditControl();
    if (target == nullptr) {
        return;
    }

    switch (command_id) {
        case kEditUndoId:
            SendMessageW(target, WM_UNDO, 0, 0);
            return;
        case kEditCutId:
            SendMessageW(target, WM_CUT, 0, 0);
            return;
        case kEditCopyId:
            SendMessageW(target, WM_COPY, 0, 0);
            return;
        case kEditPasteId:
            SendMessageW(target, WM_PASTE, 0, 0);
            return;
        case kEditDeleteId:
            SendMessageW(target, WM_CLEAR, 0, 0);
            return;
        case kEditSelectAllId:
            SendMessageW(target, EM_SETSEL, 0, -1);
            return;
        default:
            return;
    }
}

void MainWindow::HandleCreateRoom() {
    StopReconnectLoop();

    if (!winsock_.ok()) {
        SetWindowTextW(status_label_, winsock_.last_error().c_str());
        SetChildVisible(status_label_, true);
        return;
    }

    const std::wstring code = GenerateSessionCode();
    const uint16_t preferred_port = config_ != nullptr ? config_->port() : 7777;
    session_code_ = code;
    local_client_id_ = L"A";
    current_revision_ = 0;
    server_.SetDocument(last_known_text_, current_revision_);
    server_.SetSessionLogPath(storage_.data_directory() / L"session.log");
    if (!server_.Start(code, preferred_port)) {
        if (logger_ != nullptr) {
            logger_->Error(server_.last_error());
        }
        SetWindowTextW(status_label_, L"방을 만들 수 없습니다. 포트가 사용 중일 수 있습니다.");
        SetChildVisible(status_label_, true);
        return;
    }

    if (!discovery_announcer_.Start(code, server_.port())) {
        if (logger_ != nullptr) {
            logger_->Warning(discovery_announcer_.last_error());
        }
    }

    if (logger_ != nullptr) {
        logger_->Info(L"Room created");
    }

    sync_role_ = SyncRole::Host;
    std::wostringstream status;
    status << L"Hosting | 방 코드 " << code << L" | 연결 대기 중 | 포트 " << server_.port();
    ShowEditorScreen(status.str());
}

void MainWindow::HandleJoinRoom() {
    StopReconnectLoop();

    if (!winsock_.ok()) {
        SetWindowTextW(status_label_, winsock_.last_error().c_str());
        return;
    }

    const std::wstring code = GetControlText(join_code_edit_);
    if (code.empty()) {
        SetWindowTextW(status_label_, L"초대 코드를 입력하세요.");
        return;
    }

    SetWindowTextW(status_label_, L"Connecting | 방을 자동 검색 중...");
    UpdateWindow(status_label_);

    DiscoveryClient discovery;
    DiscoveryEndpoint endpoint;
    bool found = discovery.Find(code, 2500, &endpoint);

    std::wstring host;
    uint16_t port = 0;
    if (found) {
        host = endpoint.host;
        port = endpoint.port;
    } else if (advanced_join_visible_) {
        host = GetControlText(join_host_edit_);
        const std::wstring port_text = GetControlText(join_port_edit_);
        if (host.empty() || port_text.empty()) {
            SetWindowTextW(status_label_, L"주소와 포트를 입력하세요.");
            return;
        }

        try {
            const unsigned long parsed = std::stoul(port_text);
            if (parsed == 0 || parsed > 65535) {
                throw std::out_of_range("port");
            }
            port = static_cast<uint16_t>(parsed);
        } catch (...) {
            SetWindowTextW(status_label_, L"포트 번호가 올바르지 않습니다.");
            return;
        }
    } else {
        if (logger_ != nullptr) {
            logger_->Warning(discovery.last_error());
        }
        SetWindowTextW(status_label_, L"같은 네트워크에서 방을 찾을 수 없습니다.");
        return;
    }

    const std::wstring device_id = config_ != nullptr ? config_->device_id() : L"local-device";
    const std::wstring nickname = config_ != nullptr ? config_->nickname() : L"User";
    SetWindowTextW(status_label_, L"Connecting | 연결 중...");

    if (!client_.Connect(host, port, code, device_id, nickname)) {
        std::wstring message = client_.join_fail_reason().empty()
            ? FriendlySyncErrorMessage(client_.last_error())
            : FriendlyJoinFailureMessage(client_.join_fail_reason());
        if (message.empty()) {
            message = L"연결 실패";
        }
        if (logger_ != nullptr) {
            logger_->Warning(L"Join failed: " + message);
        }
        SetWindowTextW(status_label_, message.c_str());
        return;
    }

    session_code_ = code;
    local_client_id_ = client_.assigned_client().empty() ? L"B" : client_.assigned_client();
    reconnect_host_ = host;
    reconnect_port_ = port;
    reconnect_device_id_ = device_id;
    reconnect_nickname_ = nickname;
    current_revision_ = 0;
    sync_role_ = SyncRole::Guest;
    if (logger_ != nullptr) {
        logger_->Info(L"Joined room");
    }

    std::wostringstream status;
    status << L"Connected | 코드 " << code;
    ShowEditorScreen(status.str());
}

void MainWindow::HandleLeaveRoom() {
    StopReconnectLoop();
    discovery_announcer_.Stop();
    client_.Disconnect();
    server_.Stop();

    sync_role_ = SyncRole::Offline;
    session_code_.clear();
    local_client_id_ = L"A";
    reconnect_host_.clear();
    reconnect_port_ = 0;
    reconnect_device_id_.clear();
    reconnect_nickname_.clear();
    current_revision_ = 0;
    sync_response_revision_ = 0;
    sync_response_chunks_.clear();
    sync_response_received_.clear();

    ShowEditorScreen(L"Idle | 로컬 메모장");
}

void MainWindow::CopyInviteCode() {
    if (sync_role_ != SyncRole::Host || session_code_.empty()) {
        SetWindowTextW(status_label_, L"복사할 초대 코드가 없습니다. 공유 > 방 만들기를 먼저 실행하세요.");
        return;
    }

    const size_t byte_count = (session_code_.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (memory == nullptr) {
        SetWindowTextW(status_label_, L"초대 코드를 복사할 수 없습니다.");
        return;
    }

    void* locked = GlobalLock(memory);
    if (locked == nullptr) {
        GlobalFree(memory);
        SetWindowTextW(status_label_, L"초대 코드를 복사할 수 없습니다.");
        return;
    }

    std::memcpy(locked, session_code_.c_str(), byte_count);
    GlobalUnlock(memory);

    if (!OpenClipboard(hwnd_)) {
        GlobalFree(memory);
        SetWindowTextW(status_label_, L"클립보드를 열 수 없습니다.");
        return;
    }

    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        CloseClipboard();
        GlobalFree(memory);
        SetWindowTextW(status_label_, L"초대 코드를 복사할 수 없습니다.");
        return;
    }

    CloseClipboard();
    SetWindowTextW(status_label_, L"초대 코드가 복사되었습니다.");
}

void MainWindow::ShowAboutDialog() {
    MessageBoxW(
        hwnd_,
        L"ShareNotepad\n\n메모장처럼 쓰면서 같은 네트워크의 한 명과 실시간으로 공유하는 초경량 메모장입니다.",
        L"ShareNotepad 정보",
        MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ShowPopupMenu(HMENU menu, HWND anchor) {
    if (menu == nullptr || anchor == nullptr) {
        return;
    }

    RECT rect{};
    GetWindowRect(anchor, &rect);
    TrackPopupMenu(
        menu,
        TPM_LEFTALIGN | TPM_TOPALIGN,
        rect.left,
        rect.bottom + 2,
        0,
        hwnd_,
        nullptr);
}

void MainWindow::ToggleMarkdownPreview() {
    markdown_preview_visible_ = !markdown_preview_visible_;
    SetChildVisible(markdown_preview_, screen_ == Screen::Editor && markdown_preview_visible_);
    UpdateMarkdownPreview();
    UpdateMenuState();
    Layout();
    if (!markdown_preview_visible_) {
        SetFocus(editor_);
    }
}

void MainWindow::UpdateMarkdownPreview() {
    if (markdown_preview_ == nullptr || !markdown_preview_visible_) {
        return;
    }

    const std::wstring preview = RenderMarkdownPreview(GetEditorText());
    SetWindowTextW(markdown_preview_, preview.c_str());
}

void MainWindow::ApplyMarkdownShortcutIfNeeded() {
    if (editor_ == nullptr) {
        return;
    }

    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin != selection.cpMax || selection.cpMax <= 0) {
        return;
    }

    const std::wstring text = GetEditorText();
    const size_t caret = static_cast<size_t>(selection.cpMax);
    if (caret > text.size() || text[caret - 1] != L' ') {
        return;
    }

    const size_t previous_newline = text.rfind(L'\n', caret - 1);
    const size_t line_start = previous_newline == std::wstring::npos ? 0 : previous_newline + 1;
    std::wstring_view line(text.data() + line_start, caret - line_start);
    if (!line.empty() && line.front() == L'\r') {
        line.remove_prefix(1);
    }

    if (line == L"# ") {
        ApplyHeadingShortcut(line_start, 2, kHeading1Height);
        return;
    }

    if (line == L"## ") {
        ApplyHeadingShortcut(line_start, 3, kHeading2Height);
        return;
    }

    if (line == L"### ") {
        ApplyHeadingShortcut(line_start, 4, kHeading3Height);
        return;
    }

    if (line == L"- " || line == L"* ") {
        ApplyBulletShortcut(line_start, 2);
        return;
    }

    if (line == L"> ") {
        ApplyQuoteShortcut(line_start, 2);
        return;
    }

    if (line == L"``` ") {
        ApplyCodeShortcut(line_start, 4);
    }
}

void MainWindow::ResetMarkdownStyleAtLineBreakIfNeeded() {
    if (editor_ == nullptr) {
        return;
    }

    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin != selection.cpMax || selection.cpMax <= 0) {
        return;
    }

    const std::wstring text = GetEditorText();
    const size_t caret = static_cast<size_t>(selection.cpMax);
    if (caret > text.size()) {
        return;
    }

    const wchar_t previous = text[caret - 1];
    if (previous != L'\n' && previous != L'\r') {
        return;
    }

    markdown_shortcut_applying_ = true;
    ResetRichEditInsertionFormat();
    ResetRichEditParagraphFormat();
    markdown_shortcut_applying_ = false;
}

void MainWindow::ApplyHeadingShortcut(size_t line_start, size_t marker_length, LONG twip_height) {
    markdown_shortcut_applying_ = true;
    DeleteMarkdownMarker(line_start, marker_length);
    SetRichEditInsertionFormat(twip_height, true, false, L"Segoe UI");
    markdown_shortcut_applying_ = false;
    SetWindowTextW(status_label_, L"Markdown | 제목 스타일 적용");
}

void MainWindow::ApplyBulletShortcut(size_t line_start, size_t marker_length) {
    markdown_shortcut_applying_ = true;
    DeleteMarkdownMarker(line_start, marker_length);

    PARAFORMAT2 format{};
    format.cbSize = sizeof(format);
    format.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET;
    format.wNumbering = PFN_BULLET;
    format.dxStartIndent = 420;
    format.dxOffset = 240;
    SendMessageW(editor_, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
    ResetRichEditInsertionFormat();

    markdown_shortcut_applying_ = false;
    SetWindowTextW(status_label_, L"Markdown | 목록 스타일 적용");
}

void MainWindow::ApplyQuoteShortcut(size_t line_start, size_t marker_length) {
    markdown_shortcut_applying_ = true;
    DeleteMarkdownMarker(line_start, marker_length);
    SetRichEditInsertionFormat(kDefaultTextHeight, false, true, L"Segoe UI");
    markdown_shortcut_applying_ = false;
    SetWindowTextW(status_label_, L"Markdown | 인용 스타일 적용");
}

void MainWindow::ApplyCodeShortcut(size_t line_start, size_t marker_length) {
    markdown_shortcut_applying_ = true;
    DeleteMarkdownMarker(line_start, marker_length);
    SetRichEditInsertionFormat(kDefaultTextHeight, false, false, L"Consolas");
    markdown_shortcut_applying_ = false;
    SetWindowTextW(status_label_, L"Markdown | 코드 스타일 적용");
}

void MainWindow::DeleteMarkdownMarker(size_t line_start, size_t marker_length) {
    CHARRANGE marker_selection{};
    marker_selection.cpMin = static_cast<LONG>(line_start);
    marker_selection.cpMax = static_cast<LONG>(line_start + marker_length);
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&marker_selection));

    SendMessageW(editor_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));

    CHARRANGE caret_selection{};
    caret_selection.cpMin = static_cast<LONG>(line_start);
    caret_selection.cpMax = static_cast<LONG>(line_start);
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&caret_selection));
}

void MainWindow::SetRichEditInsertionFormat(LONG twip_height, bool bold, bool italic, const wchar_t* face_name) {
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_FACE | CFM_COLOR;
    format.yHeight = twip_height;
    format.crTextColor = kDarkText;
    format.dwEffects = 0;
    if (bold) {
        format.dwEffects |= CFE_BOLD;
    }
    if (italic) {
        format.dwEffects |= CFE_ITALIC;
    }
    wcsncpy_s(format.szFaceName, face_name, _TRUNCATE);
    SendMessageW(editor_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
}

void MainWindow::ResetRichEditInsertionFormat() {
    SetRichEditInsertionFormat(kDefaultTextHeight, false, false, L"Segoe UI");
}

void MainWindow::ResetRichEditParagraphFormat() {
    PARAFORMAT2 format{};
    format.cbSize = sizeof(format);
    format.dwMask = PFM_NUMBERING | PFM_STARTINDENT | PFM_OFFSET;
    format.wNumbering = 0;
    format.dxStartIndent = 0;
    format.dxOffset = 0;
    SendMessageW(editor_, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&format));
}

std::wstring MainWindow::RenderMarkdownPreview(std::wstring_view markdown) const {
    std::wstring preview;
    std::wstring_view rest = markdown;
    bool in_code_block = false;

    while (!rest.empty()) {
        size_t line_end = rest.find_first_of(L"\r\n");
        std::wstring_view line = line_end == std::wstring_view::npos ? rest : rest.substr(0, line_end);
        if (line_end == std::wstring_view::npos) {
            rest = {};
        } else {
            size_t next = line_end + 1;
            if (rest[line_end] == L'\r' && next < rest.size() && rest[next] == L'\n') {
                ++next;
            }
            rest.remove_prefix(next);
        }

        std::wstring_view trimmed = TrimLeft(line);
        if (StartsWith(trimmed, L"```")) {
            in_code_block = !in_code_block;
            preview.append(in_code_block ? L"[code]\r\n" : L"[/code]\r\n");
            continue;
        }

        if (in_code_block) {
            preview.append(line);
            preview.append(L"\r\n");
            continue;
        }

        size_t heading_level = 0;
        while (heading_level < trimmed.size() && heading_level < 6 && trimmed[heading_level] == L'#') {
            ++heading_level;
        }
        if (heading_level > 0 && heading_level < trimmed.size() && trimmed[heading_level] == L' ') {
            std::wstring heading = StripInlineMarkdown(trimmed.substr(heading_level + 1));
            preview.append(heading);
            preview.append(L"\r\n");
            preview.append(heading_level <= 2 ? L"================" : L"----------------");
            preview.append(L"\r\n");
            continue;
        }

        if (StartsWith(trimmed, L"> ")) {
            preview.append(L"> ");
            preview.append(StripInlineMarkdown(trimmed.substr(2)));
            preview.append(L"\r\n");
            continue;
        }

        if (StartsWith(trimmed, L"- [ ] ") || StartsWith(trimmed, L"* [ ] ")) {
            preview.append(L"[ ] ");
            preview.append(StripInlineMarkdown(trimmed.substr(6)));
            preview.append(L"\r\n");
            continue;
        }

        if (StartsWith(trimmed, L"- [x] ") || StartsWith(trimmed, L"- [X] ") ||
            StartsWith(trimmed, L"* [x] ") || StartsWith(trimmed, L"* [X] ")) {
            preview.append(L"[x] ");
            preview.append(StripInlineMarkdown(trimmed.substr(6)));
            preview.append(L"\r\n");
            continue;
        }

        if (StartsWith(trimmed, L"- ") || StartsWith(trimmed, L"* ") || StartsWith(trimmed, L"+ ")) {
            preview.append(L"- ");
            preview.append(StripInlineMarkdown(trimmed.substr(2)));
            preview.append(L"\r\n");
            continue;
        }

        preview.append(StripInlineMarkdown(line));
        preview.append(L"\r\n");
    }

    return preview;
}

void MainWindow::HideJoinControls() {
    SetJoinControlsVisible(false);
}

void MainWindow::SetJoinControlsVisible(bool visible) {
    SetChildVisible(join_title_label_, visible);
    SetChildVisible(join_code_label_, visible);
    SetChildVisible(join_code_edit_, visible);
    SetChildVisible(join_host_label_, visible && advanced_join_visible_);
    SetChildVisible(join_host_edit_, visible && advanced_join_visible_);
    SetChildVisible(join_port_label_, visible && advanced_join_visible_);
    SetChildVisible(join_port_edit_, visible && advanced_join_visible_);
    SetChildVisible(connect_button_, visible);
    SetChildVisible(back_button_, visible);
    SetChildVisible(advanced_button_, visible);
}

void MainWindow::ToggleAdvancedJoin() {
    advanced_join_visible_ = !advanced_join_visible_;
    SetWindowTextW(advanced_button_, advanced_join_visible_ ? L"고급 숨기기" : L"고급 연결");
    SetJoinControlsVisible(true);
    Layout();
}

void MainWindow::UpdateMenuState() {
    if (menu_ == nullptr) {
        return;
    }

    const UINT enabled = MF_BYCOMMAND | MF_ENABLED;
    const UINT disabled = MF_BYCOMMAND | MF_GRAYED;
    const bool sharing = sync_role_ != SyncRole::Offline;
    const bool hosting = sync_role_ == SyncRole::Host && !session_code_.empty();

    CheckMenuItem(menu_, kViewMarkdownPreviewId, MF_BYCOMMAND | (markdown_preview_visible_ ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu_, kHostButtonId, sharing ? disabled : enabled);
    EnableMenuItem(menu_, kJoinButtonId, sharing ? disabled : enabled);
    EnableMenuItem(menu_, kShareCopyCodeId, hosting ? enabled : disabled);
    EnableMenuItem(menu_, kShareLeaveRoomId, sharing ? enabled : disabled);
    DrawMenuBar(hwnd_);
}

std::wstring MainWindow::CurrentEditorStatusText() const {
    if (sync_role_ == SyncRole::Host && !session_code_.empty()) {
        std::wostringstream status;
        status << L"Hosting | 방 코드 " << session_code_ << L" | 포트 " << server_.port();
        return status.str();
    }

    if (sync_role_ == SyncRole::Guest && !session_code_.empty()) {
        return L"Connected | 코드 " + session_code_;
    }

    return L"Idle | 로컬 메모장";
}

std::wstring MainWindow::FriendlyJoinFailureMessage(std::wstring reason) const {
    if (reason == L"invalid_code") {
        return L"초대 코드가 올바르지 않습니다.";
    }

    if (reason == L"blocked") {
        return L"잘못된 접속이 반복되어 이 방 접속이 잠시 차단되었습니다.";
    }

    if (reason == L"room_full") {
        return L"방이 가득 찼습니다. ShareNotepad MVP는 2명까지만 지원합니다.";
    }

    if (reason == L"protocol_mismatch") {
        return L"프로그램 버전 또는 통신 방식이 맞지 않습니다.";
    }

    if (reason.empty()) {
        return L"연결 실패";
    }

    return L"연결 실패: " + reason;
}

std::wstring MainWindow::FriendlySyncErrorMessage(std::wstring reason) const {
    if (reason == L"connect timeout") {
        return L"연결 시간이 초과되었습니다.";
    }

    if (reason == L"connect failed") {
        return L"연결할 수 없습니다. 같은 네트워크인지 확인하세요.";
    }

    if (reason == L"connection closed") {
        return L"상대 연결이 종료되었습니다.";
    }

    if (reason == L"heartbeat timeout") {
        return L"상대 응답이 없어 연결이 끊겼습니다.";
    }

    if (reason == L"insert_too_large") {
        return L"한 번에 붙여넣는 내용이 너무 큽니다.";
    }

    if (reason == L"document_too_large") {
        return L"문서 크기 제한을 초과했습니다.";
    }

    if (reason == L"invalid_session") {
        return L"세션 정보가 맞지 않아 동기화를 거절했습니다.";
    }

    if (reason == L"invalid_client") {
        return L"알 수 없는 클라이언트 요청을 거절했습니다.";
    }

    if (reason == L"invalid_position") {
        return L"문서 위치가 맞지 않아 전체 동기화를 요청합니다.";
    }

    if (reason.empty()) {
        return L"오류가 발생했습니다.";
    }

    return L"오류: " + reason;
}

std::wstring MainWindow::GenerateSessionCode() const {
    std::random_device random_device;
    std::mt19937 engine(random_device());
    std::uniform_int_distribution<int> distribution(0, 999999);

    std::wostringstream builder;
    builder << std::setw(6) << std::setfill(L'0') << distribution(engine);
    return builder.str();
}

}  // namespace lightnote
