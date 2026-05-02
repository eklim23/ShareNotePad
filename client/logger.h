#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace lightnote {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

class Logger {
public:
    Logger();
    explicit Logger(std::filesystem::path data_directory);

    bool Initialize();
    bool Log(LogLevel level, std::wstring_view message);

    bool Debug(std::wstring_view message);
    bool Info(std::wstring_view message);
    bool Warning(std::wstring_view message);
    bool Error(std::wstring_view message);

    const std::filesystem::path& data_directory() const;
    const std::wstring& last_error() const;

private:
    bool ShouldWrite(LogLevel level) const;
    std::filesystem::path LogPath() const;
    std::wstring FormatLine(LogLevel level, std::wstring_view message) const;

    std::filesystem::path data_directory_;
    LogLevel min_level_;
    std::wstring last_error_;
    std::mutex mutex_;
};

}  // namespace lightnote

