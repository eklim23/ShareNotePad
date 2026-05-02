#pragma once

#include "editor_diff.h"

#include <string>
#include <string_view>

namespace lightnote {

enum class DocumentApplyStatus {
    Applied,
    InvalidOperation,
    InvalidPosition,
    InsertTooLarge,
    DocumentTooLarge,
};

DocumentApplyStatus ApplyInsertToContent(std::wstring* content, size_t position, std::wstring_view text);
DocumentApplyStatus ApplyDeleteToContent(std::wstring* content, size_t position, size_t length);
std::wstring DocumentApplyStatusToString(DocumentApplyStatus status);

}  // namespace lightnote
