#include "editor_diff.h"

#include <iostream>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

bool ExpectSingleInsert(
    const lightnote::EditorDiffResult& diff,
    size_t position,
    const std::wstring& text) {
    return Expect(diff.valid, L"diff should be valid") &&
           Expect(diff.changed, L"diff should be changed") &&
           Expect(diff.operations.size() == 1, L"expected one operation") &&
           Expect(diff.operations[0].type == lightnote::EditorOperationType::Insert, L"expected insert") &&
           Expect(diff.operations[0].position == position, L"insert position mismatch") &&
           Expect(diff.operations[0].text == text, L"insert text mismatch");
}

bool ExpectSingleDelete(
    const lightnote::EditorDiffResult& diff,
    size_t position,
    size_t length) {
    return Expect(diff.valid, L"diff should be valid") &&
           Expect(diff.changed, L"diff should be changed") &&
           Expect(diff.operations.size() == 1, L"expected one operation") &&
           Expect(diff.operations[0].type == lightnote::EditorOperationType::Delete, L"expected delete") &&
           Expect(diff.operations[0].position == position, L"delete position mismatch") &&
           Expect(diff.operations[0].length == length, L"delete length mismatch");
}

}  // namespace

int wmain() {
    if (!ExpectSingleInsert(lightnote::ComputeEditorDiff(L"안녕", L"안녕하세요"), 2, L"하세요")) return 1;
    if (!ExpectSingleInsert(lightnote::ComputeEditorDiff(L"abef", L"abcdef"), 2, L"cd")) return 1;
    if (!ExpectSingleInsert(lightnote::ComputeEditorDiff(L"world", L"hello world"), 0, L"hello ")) return 1;

    if (!ExpectSingleDelete(lightnote::ComputeEditorDiff(L"안녕하세요", L"안녕"), 2, 3)) return 1;
    if (!ExpectSingleDelete(lightnote::ComputeEditorDiff(L"abcdef", L"abef"), 2, 2)) return 1;

    const auto replace = lightnote::ComputeEditorDiff(L"hello", L"안녕");
    if (!Expect(replace.valid && replace.changed, L"replace should be valid")) return 1;
    if (!Expect(replace.operations.size() == 2, L"replace should be two operations")) return 1;
    if (!Expect(replace.operations[0].type == lightnote::EditorOperationType::Delete &&
                    replace.operations[0].position == 0 &&
                    replace.operations[0].length == 5,
                L"replace delete mismatch")) return 1;
    if (!Expect(replace.operations[1].type == lightnote::EditorOperationType::Insert &&
                    replace.operations[1].position == 0 &&
                    replace.operations[1].text == L"안녕",
                L"replace insert mismatch")) return 1;

    const auto unchanged = lightnote::ComputeEditorDiff(L"same", L"same");
    if (!Expect(unchanged.valid && !unchanged.changed && unchanged.operations.empty(), L"unchanged mismatch")) return 1;

    std::wstring huge(lightnote::kMaxDocumentChars + 1, L'x');
    const auto too_large = lightnote::ComputeEditorDiff(L"", huge);
    if (!Expect(!too_large.valid && too_large.document_too_large, L"too large document should fail")) return 1;

    std::wcout << L"editor diff smoke passed\n";
    return 0;
}

