#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace lightnote {

class Config {
public:
    Config();
    explicit Config(std::filesystem::path data_directory);

    bool LoadOrCreate();
    bool Save() const;

    const std::wstring& device_id() const;
    const std::wstring& nickname() const;
    uint16_t port() const;

    void set_nickname(std::wstring nickname);
    void set_port(uint16_t port);

    const std::filesystem::path& data_directory() const;
    const std::wstring& last_error() const;

private:
    std::filesystem::path ConfigPath() const;
    bool LoadExisting(bool* changed);
    void ApplyDefaults(bool* changed);
    void SetLastError(std::wstring message);

    std::filesystem::path data_directory_;
    std::wstring device_id_;
    std::wstring nickname_;
    uint16_t port_ = 7777;
    mutable std::wstring last_error_;
};

}  // namespace lightnote

