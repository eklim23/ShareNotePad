#include "paths.h"

#include <windows.h>

namespace lightnote {

std::filesystem::path DefaultDataDirectory() {
    wchar_t user_profile[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(L"USERPROFILE", user_profile, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return std::filesystem::current_path() / L"ShareNotepad";
    }

    return std::filesystem::path(user_profile) / L"Documents" / L"ShareNotepad";
}

}  // namespace lightnote
