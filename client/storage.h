#pragma once

#include <filesystem>
#include <string>

namespace lightnote {

class Storage {
public:
    Storage();
    explicit Storage(std::filesystem::path data_directory);

    bool EnsureDataDirectory();
    bool LoadCurrent(std::wstring* content);
    bool SaveCurrent(const std::wstring& content);

    const std::filesystem::path& data_directory() const;
    const std::wstring& last_error() const;

private:
    std::filesystem::path CurrentPath() const;
    std::filesystem::path TempPath() const;
    void SetLastErrorMessage(const wchar_t* context);

    std::filesystem::path data_directory_;
    std::wstring last_error_;
};

}  // namespace lightnote
