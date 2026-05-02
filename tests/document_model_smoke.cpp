#include "document_model.h"

#include <iostream>

namespace {

bool Expect(bool condition, const wchar_t* message) {
    if (!condition) {
        std::wcerr << message << L"\n";
        return false;
    }
    return true;
}

}  // namespace

int wmain() {
    std::wstring text = L"helo";
    if (!Expect(lightnote::ApplyInsertToContent(&text, 2, L"l") == lightnote::DocumentApplyStatus::Applied,
                L"insert should apply")) return 1;
    if (!Expect(text == L"hello", L"insert content mismatch")) return 1;

    if (!Expect(lightnote::ApplyDeleteToContent(&text, 1, 3) == lightnote::DocumentApplyStatus::Applied,
                L"delete should apply")) return 1;
    if (!Expect(text == L"ho", L"delete content mismatch")) return 1;

    if (!Expect(lightnote::ApplyInsertToContent(&text, 99, L"x") == lightnote::DocumentApplyStatus::InvalidPosition,
                L"invalid insert position should fail")) return 1;
    if (!Expect(lightnote::ApplyDeleteToContent(&text, 1, 99) == lightnote::DocumentApplyStatus::InvalidPosition,
                L"invalid delete range should fail")) return 1;
    if (!Expect(lightnote::ApplyInsertToContent(&text, 0, L"") == lightnote::DocumentApplyStatus::InvalidOperation,
                L"empty insert should fail")) return 1;
    if (!Expect(lightnote::ApplyDeleteToContent(&text, 0, 0) == lightnote::DocumentApplyStatus::InvalidOperation,
                L"empty delete should fail")) return 1;

    std::wstring huge(lightnote::kMaxDocumentChars, L'x');
    if (!Expect(lightnote::ApplyInsertToContent(&huge, huge.size(), L"y") ==
                    lightnote::DocumentApplyStatus::DocumentTooLarge,
                L"too large insert should fail")) return 1;

    std::wcout << L"document model smoke passed\n";
    return 0;
}
