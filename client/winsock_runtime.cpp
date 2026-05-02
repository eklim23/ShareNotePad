#include "winsock_runtime.h"

#include <winsock2.h>

namespace lightnote {

WinSockRuntime::WinSockRuntime() {
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        last_error_ = L"WSAStartup failed";
        return;
    }

    initialized_ = true;
}

WinSockRuntime::~WinSockRuntime() {
    if (initialized_) {
        WSACleanup();
    }
}

bool WinSockRuntime::ok() const {
    return initialized_;
}

const std::wstring& WinSockRuntime::last_error() const {
    return last_error_;
}

}  // namespace lightnote

