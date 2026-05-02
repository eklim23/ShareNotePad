#include "app.h"

namespace lightnote {

App::App(HINSTANCE instance)
    : instance_(instance), config_(), logger_(config_.data_directory()), main_window_(instance, &config_, &logger_) {}

bool App::Initialize(int show_command) {
    if (!config_.LoadOrCreate()) {
        return false;
    }

    logger_.Initialize();
    logger_.Info(L"ShareNotepad starting");

    if (!main_window_.Create()) {
        logger_.Error(L"Main window creation failed");
        return false;
    }

    main_window_.Show(show_command);
    return true;
}

int App::Run() {
    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    logger_.Info(L"ShareNotepad exiting");
    return static_cast<int>(message.wParam);
}

}  // namespace lightnote
