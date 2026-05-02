#pragma once

#include "config.h"
#include "logger.h"
#ifdef SHARENOTEPAD_USE_WEBVIEW2_UI
#include "webview_window.h"
#else
#include "window.h"
#endif

#include <windows.h>

namespace lightnote {

class App {
public:
    explicit App(HINSTANCE instance);

    bool Initialize(int show_command);
    int Run();

private:
    HINSTANCE instance_;
    Config config_;
    Logger logger_;
#ifdef SHARENOTEPAD_USE_WEBVIEW2_UI
    WebViewWindow main_window_;
#else
    MainWindow main_window_;
#endif
};

}  // namespace lightnote
