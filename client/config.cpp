#include "config.h"

#include "paths.h"
#include "text_encoding.h"

#include <objbase.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace lightnote {
namespace {

constexpr uint16_t kDefaultPort = 7777;
constexpr wchar_t kDefaultNickname[] = L"User";

std::wstring Trim(std::wstring value) {
    auto is_space = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };

    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::wstring GenerateDeviceId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return L"local-device";
    }

    wchar_t buffer[39]{};
    if (StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) == 0) {
        return L"local-device";
    }

    std::wstring value(buffer);
    if (value.size() >= 2 && value.front() == L'{' && value.back() == L'}') {
        value = value.substr(1, value.size() - 2);
    }

    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

bool ParsePort(const std::wstring& value, uint16_t* port) {
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0 || parsed > 65535) {
            return false;
        }
        *port = static_cast<uint16_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool EnsureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
}

bool ReadUtf8File(const std::filesystem::path& path, std::wstring* text) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string_view payload(bytes);
    if (payload.size() >= 3 &&
        static_cast<unsigned char>(payload[0]) == 0xEF &&
        static_cast<unsigned char>(payload[1]) == 0xBB &&
        static_cast<unsigned char>(payload[2]) == 0xBF) {
        payload.remove_prefix(3);
    }

    return Utf8ToWide(payload, text);
}

}  // namespace

Config::Config() : data_directory_(DefaultDataDirectory()) {}

Config::Config(std::filesystem::path data_directory)
    : data_directory_(std::move(data_directory)) {}

bool Config::LoadOrCreate() {
    if (!EnsureDirectory(data_directory_)) {
        SetLastError(L"설정 폴더를 만들 수 없습니다.");
        return false;
    }

    bool changed = false;
    if (std::filesystem::exists(ConfigPath())) {
        if (!LoadExisting(&changed)) {
            return false;
        }
    } else {
        changed = true;
    }

    ApplyDefaults(&changed);
    if (changed && !Save()) {
        return false;
    }

    last_error_.clear();
    return true;
}

bool Config::Save() const {
    if (!EnsureDirectory(data_directory_)) {
        last_error_ = L"설정 폴더를 만들 수 없습니다.";
        return false;
    }

    std::wostringstream builder;
    builder << L"device_id=" << device_id_ << L"\n";
    builder << L"nickname=" << nickname_ << L"\n";
    builder << L"port=" << port_ << L"\n";

    const std::string bytes = WideToUtf8(builder.str());
    if (bytes.empty()) {
        last_error_ = L"설정을 UTF-8로 변환할 수 없습니다.";
        return false;
    }

    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    if (!file) {
        last_error_ = L"config.ini를 저장할 수 없습니다.";
        return false;
    }

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
        last_error_ = L"config.ini 쓰기에 실패했습니다.";
        return false;
    }

    last_error_.clear();
    return true;
}

const std::wstring& Config::device_id() const {
    return device_id_;
}

const std::wstring& Config::nickname() const {
    return nickname_;
}

uint16_t Config::port() const {
    return port_;
}

void Config::set_nickname(std::wstring nickname) {
    nickname_ = std::move(nickname);
}

void Config::set_port(uint16_t port) {
    port_ = port;
}

const std::filesystem::path& Config::data_directory() const {
    return data_directory_;
}

const std::wstring& Config::last_error() const {
    return last_error_;
}

std::filesystem::path Config::ConfigPath() const {
    return data_directory_ / L"config.ini";
}

bool Config::LoadExisting(bool* changed) {
    std::wstring text;
    if (!ReadUtf8File(ConfigPath(), &text)) {
        SetLastError(L"config.ini를 읽을 수 없습니다.");
        return false;
    }

    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == L'#' || line.front() == L';') {
            continue;
        }

        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }

        const std::wstring key = Trim(line.substr(0, equals));
        const std::wstring value = Trim(line.substr(equals + 1));

        if (key == L"device_id") {
            device_id_ = value;
        } else if (key == L"nickname") {
            nickname_ = value;
        } else if (key == L"port") {
            uint16_t parsed = kDefaultPort;
            if (ParsePort(value, &parsed)) {
                port_ = parsed;
            } else {
                port_ = kDefaultPort;
                *changed = true;
            }
        }
    }

    return true;
}

void Config::ApplyDefaults(bool* changed) {
    if (device_id_.empty()) {
        device_id_ = GenerateDeviceId();
        *changed = true;
    }

    if (nickname_.empty()) {
        nickname_ = kDefaultNickname;
        *changed = true;
    }

    if (port_ == 0) {
        port_ = kDefaultPort;
        *changed = true;
    }
}

void Config::SetLastError(std::wstring message) {
    last_error_ = std::move(message);
}

}  // namespace lightnote

