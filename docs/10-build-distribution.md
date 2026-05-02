# 빌드와 배포 문서

## 개발 환경

권장:

- Windows 10 또는 Windows 11
- Visual Studio 2022
- C++17 이상
- Windows SDK
- x64 Release 빌드

외부 런타임 의존성은 최소화한다.

현재 WebView2 UI 버전은 Windows WebView2 Runtime이 필요하다.
Windows 10/11 최신 환경에는 보통 이미 설치되어 있지만, 없는 PC에서는 Microsoft Edge WebView2 Runtime 설치가 필요하다.

## 프로젝트 형식

MVP 권장:

- Visual Studio C++ Desktop Application
- Win32 API
- 정적 또는 기본 런타임 링크 정책은 빌드 크기와 배포 편의성 비교 후 결정

## 빌드 타깃

| 설정 | 목적 |
| --- | --- |
| Debug x64 | 개발, 로그 상세 |
| Release x64 | 배포 |

32비트 빌드는 MVP에서 우선하지 않는다.

## 링커/라이브러리

필요 라이브러리:

- `Ws2_32.lib`
- `Comctl32.lib`

Win32 common controls 사용 시 초기화:

```cpp
InitCommonControlsEx(...);
```

WinSock 사용 시:

```cpp
WSAStartup(MAKEWORD(2, 2), &wsaData);
```

## 배포 파일

MVP 배포물:

```text
ShareNotepad.exe
WebView2Loader.dll
ui/
README.txt
VERSION.txt
ShareNotepad.portable
```

선택:

```text
LICENSE.txt
```

`ShareNotepad.portable`은 다운로드/압축 해제 폴더에서 실행된 앱이 첫 실행 self-install을 수행해야 함을 알리는 마커 파일이다.
설치된 앱 폴더에는 이 마커를 복사하지 않는다.
앱 아이콘은 `assets/ShareNotepad.ico`를 `ShareNotepad.exe` 리소스로 포함한다.
시작 메뉴 바로가기는 설치된 exe의 0번 아이콘을 사용한다.

설정과 문서는 실행 후 사용자 Documents 폴더에 생성한다.
개발용 smoke test exe, `.lib`, `.pdb`, `ShareNotepad.exe.WebView2` 캐시 폴더는 배포물에 포함하지 않는다.

## Self-install 동작

배포 폴더에서 `ShareNotepad.exe`를 실행하면 다음 순서로 동작한다.

1. 실행 파일 옆의 `ShareNotepad.portable` 마커 확인
2. `%LOCALAPPDATA%\Programs\ShareNotepad` 폴더 생성
3. `ShareNotepad.exe`, `WebView2Loader.dll`, `ui/`, `README.txt`, `VERSION.txt` 복사
4. 시작 메뉴에 `ShareNotepad` 바로가기 생성
5. 설치된 `ShareNotepad.exe`를 다시 실행
6. 다운로드 폴더에서 실행된 원래 프로세스 종료

이 방식은 개발자 용어로 `self-installing per-user app` 또는 `per-user bootstrapper`에 가깝다.

## 배포 폴더 생성

아래 명령으로 `dist\ShareNotepad` 폴더를 생성한다.

```powershell
.\scripts\package-release.ps1
```

생성 결과:

```text
dist
└─ ShareNotepad
   ├─ ShareNotepad.exe
   ├─ WebView2Loader.dll
   ├─ ui
   │  ├─ index.html
   │  ├─ styles.css
   │  └─ app.js
   ├─ README.txt
   ├─ VERSION.txt
   └─ ShareNotepad.portable
```

## 실행 시 생성되는 파일

```text
%USERPROFILE%\Documents\ShareNotepad
 ├─ current.txt
 ├─ backup_*.txt
 ├─ session.log
 ├─ config.ini
 └─ sharenotepad.log
```

## 방화벽 안내

호스트 모드에서 Windows 방화벽 허용 팝업이 뜰 수 있다.

사용자 안내 문구:

```text
방을 만들려면 Windows 방화벽에서 ShareNotepad의 네트워크 접근을 허용해야 할 수 있습니다.
```

MVP에서는 관리자 권한 상승이나 방화벽 규칙 자동 추가를 하지 않는다.

## 성능 목표

| 항목 | 목표 |
| --- | --- |
| 실행 시간 | 1초 이내 |
| 대기 CPU | 1% 이하 |
| 메모리 사용량 | 30MB 이하 |
| 내부망 반영 지연 | 100ms 이하 |
| 문서 크기 | 1MB~5MB |

## Release 전 확인

배포 전 체크리스트:

- Debug 로그 과다 출력 제거
- Release x64 빌드 성공
- 새 Windows 사용자 계정에서 실행 확인
- Documents 폴더 생성 확인
- 방 만들기/방 들어가기 확인
- 방화벽 안내 확인
- 강제 종료 후 current.txt 복구 확인



