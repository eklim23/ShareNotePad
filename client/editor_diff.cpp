#include "editor_diff.h"

#include <algorithm>

namespace lightnote {

EditorDiffResult ComputeEditorDiff(std::wstring_view old_text, std::wstring_view new_text) {
    EditorDiffResult result;

    if (new_text.size() > kMaxDocumentChars) {
        result.document_too_large = true;
        return result;
    }

    result.valid = true;
    if (old_text == new_text) {
        result.changed = false;
        return result;
    }

    result.changed = true;

    size_t prefix = 0;
    const size_t min_size = std::min(old_text.size(), new_text.size());
    while (prefix < min_size && old_text[prefix] == new_text[prefix]) {
        ++prefix;
    }

    size_t old_suffix = old_text.size();
    size_t new_suffix = new_text.size();
    while (old_suffix > prefix &&
           new_suffix > prefix &&
           old_text[old_suffix - 1] == new_text[new_suffix - 1]) {
        --old_suffix;
        --new_suffix;
    }

    const size_t deleted_length = old_suffix - prefix;
    const std::wstring inserted_text(new_text.substr(prefix, new_suffix - prefix));

    if (deleted_length > 0) {
        EditorOperation operation;
        operation.type = EditorOperationType::Delete;
        operation.position = prefix;
        operation.length = deleted_length;
        result.operations.push_back(std::move(operation));
    }

    if (!inserted_text.empty()) {
        EditorOperation operation;
        operation.type = EditorOperationType::Insert;
        operation.position = prefix;
        operation.text = inserted_text;
        operation.length = inserted_text.size();
        result.operations.push_back(std::move(operation));
    }

    return result;
}

}  // namespace lightnote

