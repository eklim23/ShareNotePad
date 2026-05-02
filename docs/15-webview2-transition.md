# ShareNotepad WebView2 UI 전환 문서

이 문서는 ShareNotepad의 UI를 기존 Win32 컨트롤 중심 화면에서 WebView2 기반 화면으로 전환하기 위한 기준이다.

## 1. 전환 이유

기존 Win32 UI는 가볍고 빠르지만, Windows 11 메모장에 가까운 탭/명령바/편집 경험을 세밀하게 만들기 어렵다.

ShareNotepad는 초경량만 고집하기보다 사용자가 계속 쓰고 싶어지는 화면 품질을 우선하기로 한다. 따라서 UI는 WebView2로 옮기고, 저장/동기화/네트워크 코어는 기존 C++ 코드를 유지한다.

## 2. 목표 구조

```text
ShareNotepad.exe
├─ C++ native core
│  ├─ config
│  ├─ storage
│  ├─ protocol
│  ├─ discovery
│  ├─ sync_client
│  └─ sync_server
└─ WebView2 UI
   ├─ ui/index.html
   ├─ ui/styles.css
   └─ ui/app.js
```

## 3. 단계별 전환

1. WebView2 SDK 패키지 추가
2. WebView2 창 생성
3. HTML/CSS/JS 기반 메모장 UI 로드
4. C++에서 초기 문서 내용을 Web UI로 전달
5. Web UI 입력을 C++로 전달
6. C++ 자동 저장과 Web UI 상태바 연결
7. 방 만들기/방 들어가기 명령 연결
8. 동기화 이벤트를 Web UI 편집 영역에 반영
9. 기존 Win32 UI 제거 또는 fallback 전용으로 축소

## 4. 브릿지 메시지 원칙

Web UI와 C++ 코어는 JSON 문자열로만 통신한다.

```json
{
  "type": "editorChanged",
  "text": "문서 내용"
}
```

초기 메시지 타입:

| 방향 | type | 목적 |
| --- | --- | --- |
| Web -> C++ | `uiReady` | UI 로드 완료 |
| Web -> C++ | `editorChanged` | 문서 내용 변경 |
| Web -> C++ | `createRoom` | 방 만들기 요청 |
| Web -> C++ | `joinRoom` | 방 들어가기 요청 |
| C++ -> Web | `hydrateDocument` | 초기 문서 주입 |
| C++ -> Web | `statusChanged` | 저장/연결 상태 변경 |
| C++ -> Web | `remoteDocumentChanged` | 원격 변경 반영 |

## 5. MVP 유지 기준

- WebView2 전환 중에도 기존 저장/동기화 코어는 깨지면 안 된다.
- UI가 바뀌어도 `Documents\ShareNotepad\current.txt` 저장 위치는 유지한다.
- WebView2 Runtime이 없는 환경에서는 사용자에게 설치 필요 문구를 보여준다.
- 실제 배포 전에는 WebView2 Runtime 포함 방식 또는 설치 안내를 결정한다.
- 기존 Win32 UI는 `SHARENOTEPAD_USE_WEBVIEW2_UI=OFF` 빌드 옵션으로 fallback 유지한다.

## 6. 검증 기준

- Release 빌드 성공
- `ShareNotepad.exe` 실행 시 WebView2 화면 표시
- `ui/index.html`, `ui/styles.css`, `ui/app.js`가 exe 출력 폴더로 복사됨
- Web UI에서 C++로 `uiReady` 메시지 전달
- 좁은 창에서도 탭/명령바/편집 영역이 겹치지 않음
- 기존 스모크 테스트 통과

## 7. 현재 연결 상태

- `uiReady` 수신 후 C++이 현재 문서를 Web UI에 주입한다.
- Web UI 입력은 `editorChanged` 메시지로 C++에 전달되고 `current.txt`로 자동 저장된다.
- `createRoom`은 C++ `SyncServer`와 UDP discovery announcer를 시작한다.
- `joinRoom`은 초대 코드로 LAN discovery를 수행한 뒤 `SyncClient`로 접속한다.
- `APPLY`와 `SYNC_RESPONSE` 이벤트는 Web UI 문서 내용으로 반영된다.
