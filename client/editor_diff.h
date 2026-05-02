#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lightnote {

constexpr size_t kMaxDocumentChars = 5 * 1024 * 1024;

enum class EditorOperationType {
    Insert,
    Delete,
};

struct EditorOperation {
    EditorOperationType type = EditorOperationType::Insert;
    size_t position = 0;
    std::wstring text;
    size_t length = 0;
};

struct EditorDiffResult {
    bool valid = false;
    bool changed = false;
    bool document_too_large = false;
    std::vector<EditorOperation> operations;
};

EditorDiffResult ComputeEditorDiff(std::wstring_view old_text, std::wstring_view new_text);

}  // namespace lightnote

