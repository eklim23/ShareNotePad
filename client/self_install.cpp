#include "self_install.h"

#include <shlobj.h>
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace lightnote {
namespace {

constexpr wchar_t kAppName[] = L"ShareNotepad";
constexpr wchar_t kPortableMarker[] = L"ShareNotepad.portable";
constexpr wchar_t kInstalledExe[] = L"ShareNotepad.exe";

std::optional<std::filesystem::path> CurrentExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::nullopt;
        }

        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }

        buffer.resize(buffer.size() * 2);
    }
}

std::optional<std::filesystem::path> KnownFolderPath(const KNOWNFOLDERID& id) {
    PWSTR raw_path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw_path);
    if (FAILED(result) || raw_path == nullptr) {
        return std::nullopt;
    }

    std::filesystem::path path(raw_path);
    CoTaskMemFree(raw_path);
    return path;
}

std::optional<std::filesystem::path> InstallDirectory() {
    const std::optional<std::filesystem::path> local_app_data = KnownFolderPath(FOLDERID_LocalAppData);
    if (!local_app_data.has_value()) {
        return std::nullopt;
    }

    return local_app_data.value() / L"Programs" / kAppName;
}

std::wstring LowerPath(std::filesystem::path path) {
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error) {
        path = std::filesystem::absolute(path, error);
    }

    std::wstring text = path.wstring();
    while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }

    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return text;
}

bool SamePath(const std::filesystem::path& left, const std::filesystem::path& right) {
    return LowerPath(left) == LowerPath(right);
}

std::wstring LastErrorMessage(DWORD error) {
    if (error == ERROR_SUCCESS) {
        return L"unknown error";
    }

    wchar_t* raw_message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&raw_message),
        0,
        nullptr);

    if (length == 0 || raw_message == nullptr) {
        std::wostringstream builder;
        builder << L"error " << error;
        return builder.str();
    }

    std::wstring message(raw_message, length);
    LocalFree(raw_message);
    return message;
}

std::wstring NarrowToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return std::wstring(text.begin(), text.end());
    }

    std::wstring output(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), output.data(), required);
    return output;
}

bool CopyFileRequired(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::wstring* error_message) {
    std::error_code error;
    if (!std::filesystem::exists(source, error)) {
        *error_message = L"필수 파일을 찾을 수 없습니다: " + source.wstring();
        return false;
    }

    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        *error_message =
            L"파일 복사 실패: " + source.wstring() + L" -> " + destination.wstring() + L"\n" +
            NarrowToWide(error.message());
        return false;
    }

    return true;
}

bool CopyFileOptional(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::wstring* error_message) {
    std::error_code error;
    if (!std::filesystem::exists(source, error)) {
        return true;
    }

    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        *error_message =
            L"파일 복사 실패: " + source.wstring() + L" -> " + destination.wstring() + L"\n" +
            NarrowToWide(error.message());
        return false;
    }

    return true;
}

bool CopyDirectoryRequired(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    std::wstring* error_message) {
    std::error_code error;
    if (!std::filesystem::exists(source, error) || !std::filesystem::is_directory(source, error)) {
        *error_message = L"필수 폴더를 찾을 수 없습니다: " + source.wstring();
        return false;
    }

    std::filesystem::remove_all(destination, error);
    if (error) {
        *error_message = L"기존 UI 폴더 정리 실패: " + destination.wstring();
        return false;
    }

    std::filesystem::copy(
        source,
        destination,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        *error_message =
            L"폴더 복사 실패: " + source.wstring() + L" -> " + destination.wstring() + L"\n" +
            NarrowToWide(error.message());
        return false;
    }

    return true;
}

bool CreateStartMenuShortcut(const std::filesystem::path& installed_exe, const std::filesystem::path& install_dir) {
    const std::optional<std::filesystem::path> programs = KnownFolderPath(FOLDERID_Programs);
    if (!programs.has_value()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(programs.value(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path shortcut_path = programs.value() / L"ShareNotepad.lnk";
    const HRESULT co_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool should_uninitialize = SUCCEEDED(co_result);
    if (FAILED(co_result) && co_result != RPC_E_CHANGED_MODE) {
        return false;
    }

    IShellLinkW* shell_link = nullptr;
    HRESULT result = CoCreateInstance(
        CLSID_ShellLink,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IShellLinkW,
        reinterpret_cast<void**>(&shell_link));
    if (FAILED(result) || shell_link == nullptr) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return false;
    }

    shell_link->SetPath(installed_exe.c_str());
    shell_link->SetWorkingDirectory(install_dir.c_str());
    shell_link->SetDescription(L"ShareNotepad");
    shell_link->SetIconLocation(installed_exe.c_str(), 0);

    IPersistFile* persist_file = nullptr;
    result = shell_link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist_file));
    if (SUCCEEDED(result) && persist_file != nullptr) {
        result = persist_file->Save(shortcut_path.c_str(), TRUE);
        persist_file->Release();
    }

    shell_link->Release();

    if (should_uninitialize) {
        CoUninitialize();
    }

    return SUCCEEDED(result);
}

bool LaunchInstalledApp(const std::filesystem::path& installed_exe, const std::filesystem::path& install_dir) {
    std::wstring command_line = L"\"" + installed_exe.wstring() + L"\"";

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};

    const BOOL created = CreateProcessW(
        installed_exe.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        install_dir.c_str(),
        &startup_info,
        &process_info);
    if (!created) {
        return false;
    }

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return true;
}

void ShowInstallError(const std::wstring& message) {
    MessageBoxW(
        nullptr,
        message.c_str(),
        L"ShareNotepad 설치 실패",
        MB_ICONERROR | MB_OK);
}

}  // namespace

SelfInstallResult RunSelfInstallIfNeeded() {
    const std::optional<std::filesystem::path> current_exe = CurrentExecutablePath();
    if (!current_exe.has_value()) {
        return SelfInstallResult::Continue;
    }

    const std::filesystem::path source_dir = current_exe.value().parent_path();
    if (!std::filesystem::exists(source_dir / kPortableMarker)) {
        return SelfInstallResult::Continue;
    }

    const std::optional<std::filesystem::path> install_dir = InstallDirectory();
    if (!install_dir.has_value()) {
        ShowInstallError(L"사용자 설치 경로를 찾을 수 없습니다.");
        return SelfInstallResult::Failed;
    }

    if (SamePath(source_dir, install_dir.value())) {
        return SelfInstallResult::Continue;
    }

    std::wstring error_message;
    std::error_code error;
    std::filesystem::create_directories(install_dir.value(), error);
    if (error) {
        ShowInstallError(L"설치 폴더를 만들 수 없습니다: " + install_dir.value().wstring());
        return SelfInstallResult::Failed;
    }

    const std::filesystem::path installed_exe = install_dir.value() / kInstalledExe;
    if (!CopyFileRequired(current_exe.value(), installed_exe, &error_message) ||
        !CopyFileRequired(source_dir / L"WebView2Loader.dll", install_dir.value() / L"WebView2Loader.dll", &error_message) ||
        !CopyDirectoryRequired(source_dir / L"ui", install_dir.value() / L"ui", &error_message) ||
        !CopyFileOptional(source_dir / L"README.txt", install_dir.value() / L"README.txt", &error_message) ||
        !CopyFileOptional(source_dir / L"VERSION.txt", install_dir.value() / L"VERSION.txt", &error_message)) {
        ShowInstallError(
            error_message + L"\n\n이미 실행 중인 ShareNotepad가 있다면 닫고 다시 실행해 주세요.");
        return SelfInstallResult::Failed;
    }

    CreateStartMenuShortcut(installed_exe, install_dir.value());

    if (!LaunchInstalledApp(installed_exe, install_dir.value())) {
        ShowInstallError(L"설치된 ShareNotepad를 실행할 수 없습니다: " + LastErrorMessage(GetLastError()));
        return SelfInstallResult::Failed;
    }

    return SelfInstallResult::Relaunched;
}

}  // namespace lightnote
