#include "storage.h"

#include "paths.h"
#include "text_encoding.h"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <string_view>

namespace lightnote {
namespace {

bool HasPrefix(std::string_view bytes, std::initializer_list<unsigned char> prefix) {
    if (bytes.size() < prefix.size()) {
        return false;
    }

    size_t index = 0;
    for (unsigned char value : prefix) {
        if (static_cast<unsigned char>(bytes[index]) != value) {
            return false;
        }
        ++index;
    }

    return true;
}

}  // namespace

Storage::Storage() : data_directory_(DefaultDataDirectory()) {}

Storage::Storage(std::filesystem::path data_directory)
    : data_directory_(std::move(data_directory)) {}

bool Storage::EnsureDataDirectory() {
    std::error_code error;
    std::filesystem::create_directories(data_directory_, error);
    if (error) {
        last_error_ = L"저장 폴더를 만들 수 없습니다.";
        return false;
    }

    return true;
}

bool Storage::LoadCurrent(std::wstring* content) {
    content->clear();
    if (!EnsureDataDirectory()) {
        return false;
    }

    const std::filesystem::path current = CurrentPath();
    if (!std::filesystem::exists(current)) {
        return true;
    }

    std::ifstream file(current, std::ios::binary);
    if (!file) {
        last_error_ = L"current.txt를 열 수 없습니다.";
        return false;
    }

    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string_view payload(bytes);

    if (HasPrefix(payload, {0xEF, 0xBB, 0xBF})) {
        payload.remove_prefix(3);
        return Utf8ToWide(payload, content);
    }

    if (HasPrefix(payload, {0xFF, 0xFE})) {
        payload.remove_prefix(2);
        return Utf16LeBytesToWide(payload, content);
    }

    if (!Utf8ToWide(payload, content)) {
        last_error_ = L"current.txt의 문자 인코딩을 읽을 수 없습니다.";
        return false;
    }

    return true;
}

bool Storage::SaveCurrent(const std::wstring& content) {
    if (!EnsureDataDirectory()) {
        return false;
    }

    const std::string bytes = WideToUtf8(content);
    if (!content.empty() && bytes.empty()) {
        last_error_ = L"문서를 UTF-8로 변환할 수 없습니다.";
        return false;
    }

    const std::filesystem::path temp = TempPath();
    const std::filesystem::path current = CurrentPath();

    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            last_error_ = L"임시 저장 파일을 만들 수 없습니다.";
            return false;
        }

        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        file.flush();
        if (!file) {
            last_error_ = L"임시 저장 파일에 쓸 수 없습니다.";
            return false;
        }
    }

    if (!MoveFileExW(temp.c_str(), current.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetLastErrorMessage(L"current.txt를 교체할 수 없습니다.");
        return false;
    }

    last_error_.clear();
    return true;
}

const std::filesystem::path& Storage::data_directory() const {
    return data_directory_;
}

const std::wstring& Storage::last_error() const {
    return last_error_;
}

std::filesystem::path Storage::CurrentPath() const {
    return data_directory_ / L"current.txt";
}

std::filesystem::path Storage::TempPath() const {
    return data_directory_ / L"current.tmp";
}

void Storage::SetLastErrorMessage(const wchar_t* context) {
    last_error_ = context;
}

}  // namespace lightnote
