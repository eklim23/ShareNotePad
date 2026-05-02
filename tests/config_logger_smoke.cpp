#include "config.h"
#include "logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int wmain() {
    const std::filesystem::path test_dir =
        std::filesystem::temp_directory_path() / L"ShareNotepadConfigLoggerSmoke";

    std::error_code error;
    std::filesystem::remove_all(test_dir, error);
    if (error) {
        std::wcerr << L"cleanup failed: " << test_dir.wstring() << L"\n";
        return 1;
    }

    lightnote::Config first(test_dir);
    if (!first.LoadOrCreate()) {
        std::wcerr << L"config load/create failed: " << first.last_error() << L"\n";
        return 1;
    }

    const std::wstring first_device_id = first.device_id();
    if (first_device_id.empty() || first.nickname().empty() || first.port() != 7777) {
        std::wcerr << L"config defaults invalid\n";
        return 1;
    }

    first.set_nickname(L"테스터");
    first.set_port(7788);
    if (!first.Save()) {
        std::wcerr << L"config save failed: " << first.last_error() << L"\n";
        return 1;
    }

    lightnote::Config second(test_dir);
    if (!second.LoadOrCreate()) {
        std::wcerr << L"config reload failed: " << second.last_error() << L"\n";
        return 1;
    }

    if (second.device_id() != first_device_id || second.nickname() != L"테스터" || second.port() != 7788) {
        std::wcerr << L"config did not persist\n";
        return 1;
    }

    lightnote::Logger logger(test_dir);
    if (!logger.Initialize() || !logger.Debug(L"debug smoke") || !logger.Info(L"info smoke") ||
        !logger.Warning(L"warn smoke") || !logger.Error(L"error smoke")) {
        std::wcerr << L"logger failed: " << logger.last_error() << L"\n";
        return 1;
    }

    const std::filesystem::path log_path = test_dir / L"sharenotepad.log";
    std::ifstream log(log_path, std::ios::binary);
    const std::string log_bytes((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    if (log_bytes.find("info smoke") == std::string::npos ||
        log_bytes.find("warn smoke") == std::string::npos ||
        log_bytes.find("error smoke") == std::string::npos) {
        std::wcerr << L"log content missing\n";
        return 1;
    }

    std::filesystem::remove_all(test_dir, error);
    std::wcout << L"config/logger smoke passed\n";
    return 0;
}
