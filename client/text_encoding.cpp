#include "text_encoding.h"

#include <windows.h>

#include <cstring>

namespace lightnote {

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required <= 0) {
        return {};
    }

    std::string output(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        output.data(),
        required,
        nullptr,
        nullptr);
    return output;
}

bool Utf8ToWide(std::string_view value, std::wstring* output) {
    output->clear();
    if (value.empty()) {
        return true;
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);

    if (required <= 0) {
        return false;
    }

    output->resize(static_cast<size_t>(required));
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        output->data(),
        required);
    return true;
}

bool Utf16LeBytesToWide(std::string_view bytes, std::wstring* output) {
    output->clear();
    if (bytes.size() % sizeof(wchar_t) != 0) {
        return false;
    }

    output->resize(bytes.size() / sizeof(wchar_t));
    std::memcpy(output->data(), bytes.data(), bytes.size());
    return true;
}

}  // namespace lightnote

