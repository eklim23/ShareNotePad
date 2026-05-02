#include "app.h"
#include "self_install.h"

#include <windows.h>

namespace {

void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto set_dpi_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (set_dpi_context != nullptr &&
        set_dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }

    using SetProcessDPIAwareFn = BOOL(WINAPI*)();
    auto set_dpi_aware = reinterpret_cast<SetProcessDPIAwareFn>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (set_dpi_aware != nullptr) {
        set_dpi_aware();
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    const lightnote::SelfInstallResult install_result = lightnote::RunSelfInstallIfNeeded();
    if (install_result == lightnote::SelfInstallResult::Relaunched) {
        return 0;
    }
    if (install_result == lightnote::SelfInstallResult::Failed) {
        return 1;
    }

    EnableDpiAwareness();

    lightnote::App app(instance);
    if (!app.Initialize(show_command)) {
        return 1;
    }

    return app.Run();
}
