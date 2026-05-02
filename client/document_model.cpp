#include "document_model.h"

namespace lightnote {

DocumentApplyStatus ApplyInsertToContent(std::wstring* content, size_t position, std::wstring_view text) {
    if (content == nullptr || text.empty()) {
        return DocumentApplyStatus::InvalidOperation;
    }

    if (position > content->size()) {
        return DocumentApplyStatus::InvalidPosition;
    }

    if (content->size() + text.size() > kMaxDocumentChars) {
        return DocumentApplyStatus::DocumentTooLarge;
    }

    content->insert(position, text.data(), text.size());
    return DocumentApplyStatus::Applied;
}

DocumentApplyStatus ApplyDeleteToContent(std::wstring* content, size_t position, size_t length) {
    if (content == nullptr || length == 0) {
        return DocumentApplyStatus::InvalidOperation;
    }

    if (position > content->size() || length > content->size() - position) {
        return DocumentApplyStatus::InvalidPosition;
    }

    content->erase(position, length);
    return DocumentApplyStatus::Applied;
}

std::wstring DocumentApplyStatusToString(DocumentApplyStatus status) {
    switch (status) {
        case DocumentApplyStatus::Applied:
            return L"applied";
        case DocumentApplyStatus::InvalidOperation:
            return L"invalid_operation";
        case DocumentApplyStatus::InvalidPosition:
            return L"invalid_position";
        case DocumentApplyStatus::InsertTooLarge:
            return L"insert_too_large";
        case DocumentApplyStatus::DocumentTooLarge:
            return L"document_too_large";
        default:
            return L"unknown";
    }
}

}  // namespace lightnote
