#pragma once

#include <string>
#include <string_view>

namespace lightnote {

std::string WideToUtf8(std::wstring_view value);
bool Utf8ToWide(std::string_view value, std::wstring* output);
bool Utf16LeBytesToWide(std::string_view bytes, std::wstring* output);

}  // namespace lightnote

