#include "storage.h"

#include <filesystem>
#include <iostream>

int wmain() {
    const std::filesystem::path test_dir =
        std::filesystem::temp_directory_path() / L"ShareNotepadStorageSmoke";

    std::error_code error;
    std::filesystem::remove_all(test_dir, error);
    if (error) {
        std::wcerr << L"cleanup failed: " << test_dir.wstring() << L"\n";
        return 1;
    }

    lightnote::Storage storage(test_dir);
    const std::wstring expected = L"안녕하세요\r\nShareNotepad";

    if (!storage.SaveCurrent(expected)) {
        std::wcerr << L"save failed: " << storage.last_error() << L"\n";
        return 1;
    }

    std::wstring actual;
    if (!storage.LoadCurrent(&actual)) {
        std::wcerr << L"load failed: " << storage.last_error() << L"\n";
        return 1;
    }

    if (actual != expected) {
        std::wcerr << L"roundtrip mismatch\n";
        return 1;
    }

    std::filesystem::remove_all(test_dir, error);
    std::wcout << L"storage smoke passed\n";
    return 0;
}
