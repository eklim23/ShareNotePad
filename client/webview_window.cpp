#include "webview_window.h"

#include "document_model.h"
#include "paths.h"
#include "resource.h"

#include <dwmapi.h>
#include <objbase.h>

#include <algorithm>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>
#include <string>

namespace lightnote {
namespace {

constexpr wchar_t kWindowClassName[] = L"ShareNotepadWebViewWindow";
constexpr wchar_t kWindowTitle[] = L"ShareNotepad";
constexpr DWORD kDarkModeAttribute = 20;
constexpr UINT kWebSyncEventMessage = WM_APP + 20;

std::wstring HResultText(HRESULT result) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase << static_cast<unsigned long>(result);
    return stream.str();
}

std::wstring EscapeJson(std::wstring_view value) {
    std::wostringstream stream;
    for (wchar_t ch : value) {
        switch (ch) {
        case L'\\':
            stream << L"\\\\";
            break;
        case L'"':
            stream << L"\\\"";
            break;
        case L'\b':
            stream << L"\\b";
            break;
        case L'\f':
            stream << L"\\f";
            break;
        case L'\n':
            stream << L"\\n";
            break;
        case L'\r':
            stream << L"\\r";
            break;
        case L'\t':
            stream << L"\\t";
            break;
        default:
            if (ch < 0x20) {
                stream << L"\\u" << std::hex << std::setw(4) << std::setfill(L'0') << static_cast<int>(ch)
                       << std::dec;
            } else {
                stream << ch;
            }
            break;
        }
    }
    return stream.str();
}

std::optional<std::wstring> ExtractJsonString(std::wstring_view json, std::wstring_view key) {
    const std::wstring quoted_key = L"\"" + std::wstring(key) + L"\"";
    size_t pos = json.find(quoted_key);
    if (pos == std::wstring_view::npos) {
        return std::nullopt;
    }

    pos = json.find(L':', pos + quoted_key.size());
    if (pos == std::wstring_view::npos) {
        return std::nullopt;
    }

    pos = json.find(L'"', pos + 1);
    if (pos == std::wstring_view::npos) {
        return std::nullopt;
    }

    std::wstring value;
    bool escaping = false;
    for (size_t index = pos + 1; index < json.size(); ++index) {
        const wchar_t ch = json[index];
        if (escaping) {
            switch (ch) {
            case L'"':
            case L'\\':
            case L'/':
                value.push_back(ch);
                break;
            case L'b':
                value.push_back(L'\b');
                break;
            case L'f':
                value.push_back(L'\f');
                break;
            case L'n':
                value.push_back(L'\n');
                break;
            case L'r':
                value.push_back(L'\r');
                break;
            case L't':
                value.push_back(L'\t');
                break;
            default:
                value.push_back(ch);
                break;
            }
            escaping = false;
            continue;
        }

        if (ch == L'\\') {
            escaping = true;
            continue;
        }
        if (ch == L'"') {
            return value;
        }
        value.push_back(ch);
    }

    return std::nullopt;
}

bool HasMessageType(std::wstring_view json, std::wstring_view type) {
    const std::wstring pattern = L"\"type\":\"" + std::wstring(type) + L"\"";
    return json.find(pattern) != std::wstring_view::npos;
}

std::wstring ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);

    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }

    return path.substr(0, slash);
}

std::wstring FilePathToUri(std::wstring path) {
    std::replace(path.begin(), path.end(), L'\\', L'/');

    std::wstring uri = L"file:///";
    for (wchar_t ch : path) {
        switch (ch) {
        case L' ':
            uri += L"%20";
            break;
        case L'%':
            uri += L"%25";
            break;
        case L'#':
            uri += L"%23";
            break;
        default:
            uri.push_back(ch);
            break;
        }
    }
    return uri;
}

}  // namespace

WebViewWindow::WebViewWindow(HINSTANCE instance, Config* config, Logger* logger)
    : instance_(instance),
      config_(config),
      logger_(logger),
      storage_(config != nullptr ? config->data_directory() : DefaultDataDirectory()) {}

WebViewWindow::~WebViewWindow() {
    discovery_announcer_.Stop();
    client_.Disconnect();
    server_.Stop();
    webview_.Reset();
    controller_.Reset();
    if (com_initialized_) {
        CoUninitialize();
    }
}

bool WebViewWindow::Create() {
    const HRESULT co_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(co_result)) {
        com_initialized_ = true;
    } else if (co_result != RPC_E_CHANGED_MODE) {
        ShowWebViewError(co_result);
        return false;
    }

    if (!RegisterWindowClass()) {
        if (logger_ != nullptr) {
            logger_->Error(L"WebView window class registration failed: " + HResultText(HRESULT_FROM_WIN32(GetLastError())));
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
        1160,
        760,
        nullptr,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr) {
        if (logger_ != nullptr) {
            logger_->Error(L"WebView window creation failed: " + HResultText(HRESULT_FROM_WIN32(GetLastError())));
        }
        return false;
    }

    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(
        LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON))));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR | LR_SHARED)));

    server_.SetEventCallback([this] {
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, kWebSyncEventMessage, 0, 0);
        }
    });
    client_.SetEventCallback([this] {
        if (hwnd_ != nullptr) {
            PostMessageW(hwnd_, kWebSyncEventMessage, 0, 0);
        }
    });

    ApplyDarkWindowFrame();
    InitializeWebView();
    return true;
}

void WebViewWindow::Show(int show_command) {
    ShowWindow(hwnd_, show_command);
    UpdateWindow(hwnd_);
}

bool WebViewWindow::RegisterWindowClass() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = WebViewWindow::WindowProc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    window_class.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassW(&window_class) == 0) {
        const DWORD error = GetLastError();
        SetLastError(error);
        return error == ERROR_CLASS_ALREADY_EXISTS;
    }

    return true;
}

LRESULT CALLBACK WebViewWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WebViewWindow* window = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<WebViewWindow*>(create->lpCreateParams);
        window->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<WebViewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window != nullptr) {
        return window->HandleMessage(message, wparam, lparam);
    }

    return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT WebViewWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        ResizeWebView();
        return 0;
    case WM_DPICHANGED:
        if (lparam != 0) {
            const RECT* suggested_rect = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                hwnd_,
                nullptr,
                suggested_rect->left,
                suggested_rect->top,
                suggested_rect->right - suggested_rect->left,
                suggested_rect->bottom - suggested_rect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ResizeWebView();
        return 0;
    case kWebSyncEventMessage:
        DrainSyncEvents();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void WebViewWindow::InitializeWebView() {
    const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        nullptr,
        nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT environment_result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(environment_result) || environment == nullptr) {
                    ShowWebViewError(environment_result);
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(
                    hwnd_,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT controller_result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result) || controller == nullptr) {
                                ShowWebViewError(controller_result);
                                return S_OK;
                            }

                            controller_ = controller;
                            controller_->get_CoreWebView2(&webview_);
                            ResizeWebView();

                            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webview_->get_Settings(&settings)) && settings != nullptr) {
                                settings->put_IsScriptEnabled(TRUE);
                                settings->put_AreDefaultScriptDialogsEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            EventRegistrationToken message_token{};
                            webview_->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR raw_message = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw_message)) &&
                                            raw_message != nullptr) {
                                            HandleWebMessage(raw_message);
                                        }
                                        CoTaskMemFree(raw_message);
                                        return S_OK;
                                    })
                                    .Get(),
                                &message_token);

                            EventRegistrationToken navigation_token{};
                            webview_->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        PostInitialState();
                                        return S_OK;
                                    })
                                    .Get(),
                                &navigation_token);

                            webview_->Navigate(IndexUri().c_str());
                            return S_OK;
                        })
                        .Get());

                return S_OK;
            })
            .Get());

    if (FAILED(result)) {
        ShowWebViewError(result);
    }
}

void WebViewWindow::ResizeWebView() {
    if (controller_ == nullptr || hwnd_ == nullptr) {
        return;
    }

    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    controller_->put_Bounds(bounds);
}

void WebViewWindow::ApplyDarkWindowFrame() {
    BOOL use_dark = TRUE;
    DwmSetWindowAttribute(hwnd_, kDarkModeAttribute, &use_dark, sizeof(use_dark));
}

void WebViewWindow::HandleWebMessage(std::wstring_view message) {
    if (logger_ != nullptr) {
        logger_->Debug(L"Web UI message: " + std::wstring(message));
    }

    if (HasMessageType(message, L"uiReady")) {
        PostInitialState();
        return;
    }

    if (HasMessageType(message, L"editorChanged")) {
        const std::optional<std::wstring> text = ExtractJsonString(message, L"text");
        if (!text.has_value()) {
            PostStatus(L"자동저장 실패", L"오프라인");
            return;
        }

        const EditorDiffResult diff = ComputeEditorDiff(last_known_text_, text.value());
        if (!diff.valid) {
            PostStatus(diff.document_too_large ? L"문서 크기 초과" : L"편집 반영 실패", L"오프라인");
            PostDocument(last_known_text_);
            return;
        }

        SendLocalOperations(diff);
        last_known_text_ = text.value();

        if (storage_.SaveCurrent(text.value())) {
            const std::wstring connection = sync_role_ == SyncRole::Offline ? L"오프라인" : L"연결됨";
            PostStatus(L"자동저장 완료", connection);
        } else {
            const std::wstring connection = sync_role_ == SyncRole::Offline ? L"오프라인" : L"연결됨";
            PostStatus(L"자동저장 실패", connection);
        }
        return;
    }

    if (HasMessageType(message, L"createRoom")) {
        HandleCreateRoom(message);
        return;
    }

    if (HasMessageType(message, L"joinRoom")) {
        HandleJoinRoom(message);
        return;
    }

    if (HasMessageType(message, L"quit")) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
}

void WebViewWindow::HandleCreateRoom(std::wstring_view message) {
    if (!winsock_.ok()) {
        PostStatus(L"저장 대기", winsock_.last_error());
        return;
    }

    const std::optional<std::wstring> text = ExtractJsonString(message, L"text");
    if (text.has_value()) {
        last_known_text_ = text.value();
        storage_.SaveCurrent(last_known_text_);
    }

    discovery_announcer_.Stop();
    client_.Disconnect();
    server_.Stop();

    session_code_ = GenerateSessionCode();
    local_client_id_ = L"A";
    current_revision_ = 0;
    sync_response_revision_ = 0;
    sync_response_chunks_.clear();
    sync_response_received_.clear();

    const uint16_t preferred_port = config_ != nullptr ? config_->port() : 7777;
    server_.SetDocument(last_known_text_, current_revision_);
    server_.SetSessionLogPath(storage_.data_directory() / L"session.log");
    if (!server_.Start(session_code_, preferred_port)) {
        sync_role_ = SyncRole::Offline;
        if (logger_ != nullptr) {
            logger_->Error(L"Room create failed: " + server_.last_error());
        }
        PostStatus(L"자동저장 준비", L"방 만들기 실패");
        return;
    }

    if (!discovery_announcer_.Start(session_code_, server_.port()) && logger_ != nullptr) {
        logger_->Warning(L"Discovery announcer failed: " + discovery_announcer_.last_error());
    }

    sync_role_ = SyncRole::Host;
    PostRoomCreated(session_code_);
    PostStatus(L"자동저장 준비", L"연결 대기");
    if (logger_ != nullptr) {
        logger_->Info(L"Web room created: " + session_code_);
    }
}

void WebViewWindow::HandleJoinRoom(std::wstring_view message) {
    if (!winsock_.ok()) {
        PostStatus(L"저장 대기", winsock_.last_error());
        return;
    }

    const std::optional<std::wstring> code = ExtractJsonString(message, L"code");
    if (!code.has_value() || code->empty()) {
        PostStatus(L"저장 대기", L"초대 코드 필요");
        return;
    }

    PostStatus(L"자동저장 준비", L"방 찾는 중");

    DiscoveryClient discovery;
    DiscoveryEndpoint endpoint;
    if (!discovery.Find(code.value(), 2500, &endpoint)) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Discovery failed: " + discovery.last_error());
        }
        PostStatus(L"자동저장 준비", L"같은 네트워크에서 방을 찾지 못함");
        return;
    }

    client_.Disconnect();
    server_.Stop();
    discovery_announcer_.Stop();

    const std::wstring device_id = config_ != nullptr ? config_->device_id() : L"local-device";
    const std::wstring nickname = config_ != nullptr ? config_->nickname() : L"User";
    if (!client_.Connect(endpoint.host, endpoint.port, code.value(), device_id, nickname)) {
        std::wstring reason = client_.join_fail_reason().empty() ? client_.last_error() : client_.join_fail_reason();
        if (reason.empty()) {
            reason = L"연결 실패";
        }
        if (logger_ != nullptr) {
            logger_->Warning(L"Web join failed: " + reason);
        }
        PostStatus(L"자동저장 준비", reason);
        return;
    }

    session_code_ = code.value();
    local_client_id_ = client_.assigned_client().empty() ? L"B" : client_.assigned_client();
    current_revision_ = 0;
    sync_response_revision_ = 0;
    sync_response_chunks_.clear();
    sync_response_received_.clear();
    sync_role_ = SyncRole::Guest;
    PostStatus(L"자동저장 준비", L"연결됨");
    if (logger_ != nullptr) {
        logger_->Info(L"Web joined room: " + session_code_);
    }
}

void WebViewWindow::DrainSyncEvents() {
    SyncEvent event;
    while (server_.TryPopEvent(&event)) {
        HandleSyncEvent(event);
    }

    while (client_.TryPopEvent(&event)) {
        HandleSyncEvent(event);
    }
}

void WebViewWindow::HandleSyncEvent(const SyncEvent& event) {
    switch (event.type) {
    case SyncEventType::Apply:
        ApplySyncApply(event.message);
        return;
    case SyncEventType::SyncResponse:
        ApplySyncResponse(event.message);
        return;
    case SyncEventType::Disconnected:
        PostStatus(L"자동저장 준비", L"끊김");
        return;
    case SyncEventType::Error:
        PostStatus(L"자동저장 준비", event.detail.empty() ? L"동기화 오류" : event.detail);
        return;
    default:
        return;
    }
}

void WebViewWindow::SendLocalOperations(const EditorDiffResult& diff) {
    if (!diff.changed || sync_role_ == SyncRole::Offline) {
        return;
    }

    for (const EditorOperation& operation : diff.operations) {
        if (sync_role_ == SyncRole::Host) {
            ProtocolMessage apply;
            if (!server_.ApplyLocalOperation(operation, local_client_id_, &apply)) {
                if (logger_ != nullptr) {
                    logger_->Warning(L"Web host operation rejected: " + server_.last_error());
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

        const uint64_t base_revision = current_revision_;
        bool sent = false;
        if (operation.type == EditorOperationType::Insert) {
            sent = client_.SendInsert(base_revision, operation.position, operation.text);
        } else {
            sent = client_.SendDelete(base_revision, operation.position, operation.length);
        }

        if (!sent && logger_ != nullptr) {
            logger_->Warning(L"Web client operation send failed: " + client_.last_error());
        }
    }
}

void WebViewWindow::ApplySyncResponse(const ProtocolMessage& message) {
    std::wstring text;
    uint64_t revision = 0;
    if (!message.GetString(L"text", &text) || !message.GetNumber(L"rev", &revision)) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid web SYNC_RESPONSE");
        }
        return;
    }

    uint64_t chunk_index = 0;
    uint64_t chunk_count = 1;
    message.GetNumber(L"chunk_index", &chunk_index);
    message.GetNumber(L"chunk_count", &chunk_count);
    if (chunk_count == 0 || chunk_index >= chunk_count) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid web SYNC_RESPONSE chunk");
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
            PostStatus(L"자동저장 준비", L"전체 동기화 중");
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

void WebViewWindow::ApplyFullSyncDocument(std::wstring text, uint64_t revision) {
    last_known_text_ = std::move(text);
    current_revision_ = revision;
    storage_.SaveCurrent(last_known_text_);
    PostDocument(last_known_text_);
    PostStatus(L"자동저장 완료", L"연결됨");
}

void WebViewWindow::ApplySyncApply(const ProtocolMessage& message) {
    uint64_t revision = 0;
    uint64_t position = 0;
    std::wstring operation;
    if (!message.GetNumber(L"rev", &revision) ||
        !message.GetNumber(L"pos", &position) ||
        !message.GetString(L"op", &operation)) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Invalid web APPLY");
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

    std::wstring text = last_known_text_;
    DocumentApplyStatus status = DocumentApplyStatus::InvalidOperation;
    if (operation == L"INSERT") {
        std::wstring inserted;
        if (message.GetString(L"text", &inserted)) {
            status = ApplyInsertToContent(&text, static_cast<size_t>(position), inserted);
        }
    } else if (operation == L"DELETE") {
        uint64_t length = 0;
        if (message.GetNumber(L"len", &length)) {
            status = ApplyDeleteToContent(&text, static_cast<size_t>(position), static_cast<size_t>(length));
        }
    }

    if (status != DocumentApplyStatus::Applied) {
        if (logger_ != nullptr) {
            logger_->Warning(L"Web remote APPLY failed: " + DocumentApplyStatusToString(status));
        }
        RequestFullSync();
        return;
    }

    last_known_text_ = std::move(text);
    current_revision_ = revision;
    storage_.SaveCurrent(last_known_text_);
    PostDocument(last_known_text_);
    PostStatus(L"자동저장 완료", L"연결됨");
}

void WebViewWindow::RequestFullSync() {
    if (sync_role_ == SyncRole::Guest) {
        client_.SendSyncRequest(current_revision_);
        PostStatus(L"자동저장 준비", L"전체 동기화 요청");
        return;
    }

    if (sync_role_ == SyncRole::Host) {
        ApplyFullSyncDocument(server_.document(), server_.revision());
    }
}

void WebViewWindow::PostInitialState() {
    if (webview_ == nullptr) {
        return;
    }

    std::wstring content;
    storage_.LoadCurrent(&content);
    last_known_text_ = content;

    PostDocument(content);
    PostStatus(L"자동저장 준비", L"오프라인");
}

void WebViewWindow::PostStatus(std::wstring_view save_status, std::wstring_view connection_status) {
    if (webview_ == nullptr) {
        return;
    }

    const std::wstring status_json =
        L"{\"type\":\"statusChanged\",\"payload\":{\"saveStatus\":\"" + EscapeJson(save_status) +
        L"\",\"connectionStatus\":\"" + EscapeJson(connection_status) + L"\"}}";
    webview_->PostWebMessageAsString(status_json.c_str());
}

void WebViewWindow::PostRoomCreated(std::wstring_view code) {
    if (webview_ == nullptr) {
        return;
    }

    const std::wstring room_json =
        L"{\"type\":\"roomCreated\",\"payload\":{\"code\":\"" + EscapeJson(code) + L"\"}}";
    webview_->PostWebMessageAsString(room_json.c_str());
}

void WebViewWindow::PostDocument(std::wstring_view text) {
    if (webview_ == nullptr) {
        return;
    }

    const std::wstring document_json =
        L"{\"type\":\"hydrateDocument\",\"payload\":{\"text\":\"" + EscapeJson(text) + L"\"}}";
    webview_->PostWebMessageAsString(document_json.c_str());
}

void WebViewWindow::ShowWebViewError(HRESULT result) {
    const std::wstring message =
        L"WebView2 초기화에 실패했습니다.\n\n"
        L"Microsoft Edge WebView2 Runtime이 설치되어 있는지 확인해주세요.\n\n"
        L"오류: " +
        HResultText(result);

    if (logger_ != nullptr) {
        logger_->Error(message);
    }

    MessageBoxW(hwnd_, message.c_str(), L"ShareNotepad", MB_ICONERROR | MB_OK);
}

std::wstring WebViewWindow::IndexUri() const {
    const std::wstring path = ExecutableDirectory() + L"\\ui\\index.html";
    return FilePathToUri(path);
}

std::wstring WebViewWindow::GenerateSessionCode() const {
    std::random_device random_device;
    std::mt19937 engine(random_device());
    std::uniform_int_distribution<int> distribution(0, 999999);

    std::wostringstream builder;
    builder << std::setw(6) << std::setfill(L'0') << distribution(engine);
    return builder.str();
}

}  // namespace lightnote
