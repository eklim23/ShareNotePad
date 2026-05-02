#include "logger.h"

#include "paths.h"
#include "text_encoding.h"

#include <windows.h>

#include <fstream>

namespace lightnote {
namespace {

LogLevel DefaultMinimumLevel() {
#ifdef NDEBUG
    return LogLevel::Info;
#else
    return LogLevel::Debug;
#endif
}

const wchar_t* LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return L"DEBUG";
        case LogLevel::Info:
            return L"INFO";
        case LogLevel::Warning:
            return L"WARN";
        case LogLevel::Error:
            return L"ERROR";
        default:
            return L"INFO";
    }
}

bool EnsureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return !error;
}

}  // namespace

Logger::Logger() : data_directory_(DefaultDataDirectory()), min_level_(DefaultMinimumLevel()) {}

Logger::Logger(std::filesystem::path data_directory)
    : data_directory_(std::move(data_directory)), min_level_(DefaultMinimumLevel()) {}

bool Logger::Initialize() {
    if (!EnsureDirectory(data_directory_)) {
        last_error_ = L"로그 폴더를 만들 수 없습니다.";
        return false;
    }

    return Info(L"ShareNotepad logger initialized");
}

bool Logger::Log(LogLevel level, std::wstring_view message) {
    if (!ShouldWrite(level)) {
        return true;
    }

    const std::wstring line = FormatLine(level, message);
    const std::string bytes = WideToUtf8(line);
    if (bytes.empty()) {
        last_error_ = L"로그 메시지를 UTF-8로 변환할 수 없습니다.";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!EnsureDirectory(data_directory_)) {
        last_error_ = L"로그 폴더를 만들 수 없습니다.";
        return false;
    }

    std::ofstream file(LogPath(), std::ios::binary | std::ios::app);
    if (!file) {
        last_error_ = L"sharenotepad.log를 열 수 없습니다.";
        return false;
    }

    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
        last_error_ = L"sharenotepad.log 쓰기에 실패했습니다.";
        return false;
    }

    last_error_.clear();
    return true;
}

bool Logger::Debug(std::wstring_view message) {
    return Log(LogLevel::Debug, message);
}

bool Logger::Info(std::wstring_view message) {
    return Log(LogLevel::Info, message);
}

bool Logger::Warning(std::wstring_view message) {
    return Log(LogLevel::Warning, message);
}

bool Logger::Error(std::wstring_view message) {
    return Log(LogLevel::Error, message);
}

const std::filesystem::path& Logger::data_directory() const {
    return data_directory_;
}

const std::wstring& Logger::last_error() const {
    return last_error_;
}

bool Logger::ShouldWrite(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(min_level_);
}

std::filesystem::path Logger::LogPath() const {
    return data_directory_ / L"sharenotepad.log";
}

std::wstring Logger::FormatLine(LogLevel level, std::wstring_view message) const {
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t prefix[64]{};
    swprintf_s(
        prefix,
        L"%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu [%s] ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        LevelName(level));

    std::wstring line(prefix);
    line.append(message);
    line.push_back(L'\n');
    return line;
}

}  // namespace lightnote
