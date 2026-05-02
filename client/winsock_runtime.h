#pragma once

#include <string>

namespace lightnote {

class WinSockRuntime {
public:
    WinSockRuntime();
    ~WinSockRuntime();

    WinSockRuntime(const WinSockRuntime&) = delete;
    WinSockRuntime& operator=(const WinSockRuntime&) = delete;

    bool ok() const;
    const std::wstring& last_error() const;

private:
    bool initialized_ = false;
    std::wstring last_error_;
};

}  // namespace lightnote

