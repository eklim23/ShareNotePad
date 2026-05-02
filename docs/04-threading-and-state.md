# 스레드와 상태 머신 설계서

## 목표

UI 멈춤, 데이터 경합, 데드락을 피한다.

Win32 프로그램은 UI thread를 가볍게 유지하고, 네트워크와 저장은 별도 흐름으로 처리한다.

## 스레드 구성

| 스레드 | 역할 |
| --- | --- |
| UI thread | 창, Edit control, 상태바, 사용자 입력 |
| Client network thread | 서버 접속, 송신, 수신 |
| Server accept thread | 호스트 모드에서 접속 수락 |
| Server session thread | 메시지 처리, revision 부여 |
| Autosave timer | UI timer 또는 worker timer로 저장 예약 |

MVP에서는 복잡한 thread pool을 사용하지 않는다.

## UI thread 원칙

- Win32 control은 UI thread에서만 접근한다.
- 네트워크 thread는 `PostMessage`로 이벤트를 전달한다.
- 긴 파일 저장이나 네트워크 대기는 UI thread에서 하지 않는다.

## 이벤트 전달 방식

내부 이벤트는 다음처럼 분류한다.

```cpp
enum class AppEventType {
    Connected,
    Disconnected,
    JoinAccepted,
    JoinRejected,
    RemoteApply,
    SyncResponse,
    Error,
    SaveCompleted
};
```

네트워크 thread는 event queue에 데이터를 넣고 UI thread에 custom message를 보낸다.

```cpp
PostMessage(hwnd, WM_SHARENOTEPAD_EVENT, 0, 0);
```

## 락 정책

공유 데이터:

- `DocumentState`
- 송신 queue
- 수신 event queue
- session 상태

규칙:

- 하나의 mutex를 오래 잡지 않는다.
- 락을 잡은 상태에서 Win32 API로 UI를 변경하지 않는다.
- 락을 잡은 상태에서 `send`, `recv`, 파일 I/O를 하지 않는다.
- 문서 전체 복사는 필요할 때만 한다.

권장:

```cpp
std::mutex documentMutex;
std::mutex eventQueueMutex;
std::mutex sendQueueMutex;
```

## 애플리케이션 상태 머신

```text
Idle
 ├─ 방 만들기 -> Hosting
 └─ 방 들어가기 -> Connecting

Hosting
 ├─ 메모장 열기 -> ConnectedLocal
 └─ peer 접속 -> Connected

Connecting
 ├─ JOIN_OK -> Syncing
 ├─ JOIN_FAIL -> Idle
 └─ timeout -> Disconnected

Syncing
 ├─ SYNC_RESPONSE -> Connected
 └─ timeout -> Reconnecting

Connected
 ├─ 네트워크 끊김 -> Reconnecting
 └─ 사용자 종료 -> Closing

Reconnecting
 ├─ 재접속 성공 -> Syncing
 ├─ 사용자 취소 -> Disconnected
 └─ retry 초과 -> Disconnected

Disconnected
 ├─ 다시 연결 -> Reconnecting
 └─ 사용자 종료 -> Closing
```

## 연결 상태

```cpp
enum class ConnectionState {
    Idle,
    Hosting,
    Connecting,
    Syncing,
    Connected,
    Reconnecting,
    Disconnected,
    Closing
};
```

## 재연결 정책

재연결 간격:

| 시도 | 대기 |
| --- | --- |
| 1회 | 1초 |
| 2회 | 2초 |
| 3회 | 3초 |
| 4회 이후 | 5초 |

규칙:

- 재연결 중에도 로컬 편집은 가능하다.
- 전송되지 않은 로컬 변경은 queue에 보관한다.
- 재연결 성공 후에는 먼저 `SYNC_REQUEST`를 보낸다.
- 전체 동기화 뒤 local pending operation 처리 여부는 MVP에서는 보수적으로 접근한다.

MVP 권장:

- 재연결 전 로컬 편집이 있었다면 사용자 문서를 보존한다.
- 서버 문서와 충돌이 감지되면 로컬 백업을 만들고 서버 문서로 맞춘다.

## 종료 정책

종료 시 순서:

1. autosave 즉시 실행
2. 네트워크 thread 종료 요청
3. 서버 listen socket 닫기
4. worker thread join
5. WinSock 정리

종료 대기 시간은 최대 2초로 제한한다.



