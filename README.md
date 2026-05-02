# ShareNotepad

ShareNotepad is a lightweight Windows notepad app for two-person real-time shared editing on the same network.

It uses a native C++ core for storage, networking, discovery, and sync, with a WebView2-based UI for a modern notepad-like editing experience.

## Features

- Two-person shared notepad sessions
- Room creation and join by invite code
- Local network discovery
- Real-time insert/delete synchronization
- Auto-save to the user's Documents folder
- First-run per-user self-install
- Markdown-style block shortcuts such as headings, lists, todo, quote, code, and divider

## Storage

User documents are stored outside the app folder:

```text
%USERPROFILE%\Documents\ShareNotepad
```

The distributed app self-installs to:

```text
%LOCALAPPDATA%\Programs\ShareNotepad
```

## Build Requirements

- Windows 10 or Windows 11
- Visual Studio 2022 Build Tools with C++ desktop tools
- CMake 3.20+
- Microsoft WebView2 SDK package

Restore the WebView2 SDK before configuring from a fresh clone:

```powershell
.\scripts\restore-webview2.ps1
```

Then build:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

## Package

Create a portable self-install package folder:

```powershell
.\scripts\package-release.ps1
```

Output:

```text
dist\ShareNotepad
```

Share the whole `dist\ShareNotepad` folder as a zip. On first run, the app installs itself into the current user's LocalAppData programs folder and creates a Start Menu shortcut.

Packaged zip files such as `download.zip` are build artifacts and are intentionally not committed to this repository. Publish release zips through GitHub Releases or another download host.

## Relay Server

For school/company networks that block peer-to-peer traffic, ShareNotepad includes a lightweight relay server prototype:

```powershell
cd relay-server
go run .
```

The relay exposes:

```text
GET /healthz
GET /ws
```

See [relay-server/README.md](relay-server/README.md) for deployment details.

## Notes

- Current MVP targets same Wi-Fi / same LAN usage.
- Windows Firewall may ask for network permission when creating a room.
- WebView2 Runtime is required on the user's PC. Most updated Windows 10/11 systems already include it.
