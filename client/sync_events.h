#pragma once

#include "protocol.h"

#include <functional>
#include <string>

namespace lightnote {

enum class SyncEventType {
    Apply,
    SyncResponse,
    Error,
    Disconnected,
};

struct SyncEvent {
    SyncEventType type = SyncEventType::Error;
    ProtocolMessage message;
    std::wstring detail;
};

using SyncEventCallback = std::function<void()>;

}  // namespace lightnote
