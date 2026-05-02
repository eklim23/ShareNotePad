#pragma once

namespace lightnote {

enum class SelfInstallResult {
    Continue,
    Relaunched,
    Failed,
};

SelfInstallResult RunSelfInstallIfNeeded();

}  // namespace lightnote
