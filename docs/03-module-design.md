# 모듈 설계서

## 목표

모듈은 작고 명확해야 한다.

Win32 UI, 편집 추적, 네트워크, 저장, 프로토콜을 분리해 디버깅 가능한 구조를 만든다.

## 디렉터리 구조

```text
/client
 ├─ main.cpp
 ├─ app.cpp
 ├─ app.h
 ├─ window.cpp
 ├─ window.h
 ├─ editor.cpp
 ├─ editor.h
 ├─ sync_client.cpp
 ├─ sync_client.h
 ├─ sync_server.cpp
 ├─ sync_server.h
 ├─ protocol.cpp
 ├─ protocol.h
 ├─ storage.cpp
 ├─ storage.h
 ├─ config.cpp
 ├─ config.h
 ├─ logger.cpp
 └─ logger.h
```

## main.cpp

책임:

- 프로세스 시작점
- WinSock 초기화
- 설정 로드
- `App` 생성
- 메시지 루프 실행
- 종료 시 정리

하지 않는 일:

- 네트워크 메시지 직접 처리
- 파일 저장 직접 처리
- UI control 세부 생성

## app.cpp

책임:

- 전체 애플리케이션 상태 관리
- Host/Join 모드 전환
- UI, 저장, 네트워크 모듈 연결
- 종료 시 자동 저장 요청

주요 상태:

```cpp
enum class AppMode {
    Idle,
    Hosting,
    Joining,
    Connected,
    Reconnecting,
    Disconnected
};
```

## window.cpp

책임:

- 메인 윈도우 생성
- 버튼, Edit control, 상태바 배치
- Win32 메시지 처리
- UI thread에서만 control 변경

원칙:

- 네트워크 thread에서 직접 UI를 만지지 않는다.
- 네트워크 이벤트는 `PostMessage`로 UI thread에 전달한다.

## editor.cpp

책임:

- Edit control 관리
- 로컬 변경 감지
- 원격 변경 적용
- IME 조합 중 이벤트 억제
- 붙여넣기 감지

주요 플래그:

```cpp
bool remoteApplying;
bool imeComposing;
```

## sync_client.cpp

책임:

- 호스트 서버 접속
- HELLO/JOIN 전송
- INSERT/DELETE 전송
- APPLY/SYNC_RESPONSE 수신
- PING/PONG 처리
- 재연결 처리

원칙:

- 수신 thread는 메시지 파싱 후 event queue에 넣는다.
- 문서 변경은 UI thread 또는 app coordinator가 적용한다.

## sync_server.cpp

책임:

- 방 생성 시 TCP listen 시작
- 클라이언트 접속 수락
- 세션 코드 검증
- 최대 2명 제한
- edit operation에 revision 부여
- APPLY 메시지 broadcast
- 전체 문서 상태 보관

서버가 가진 권한:

- revision 증가
- 최종 적용 순서 결정
- 잘못된 요청 거절
- 전체 동기화 응답

## protocol.cpp

책임:

- 메시지 구조체 정의
- JSON serialize/deserialize
- TCP frame encode/decode
- 필드 검증
- UTF-8/UTF-16 변환 지원

MVP 메시지 구조체 예:

```cpp
struct InsertMessage {
    std::string session;
    std::string client;
    uint64_t baseRevision;
    size_t position;
    std::wstring text;
};
```

## storage.cpp

책임:

- 저장 경로 생성
- `current.txt` 저장
- 임시 파일 후 원자적 교체
- 백업 생성
- session log 기록
- 시작 시 복구

저장 위치:

```text
%USERPROFILE%\Documents\ShareNotepad
```

## config.cpp

책임:

- `config.ini` 로드/저장
- 최근 포트
- 최근 닉네임
- device_id
- 마지막 문서 경로

## logger.cpp

책임:

- 파일 로그 기록
- 디버그 빌드 콘솔 출력
- 네트워크/동기화/저장 오류 추적

로그 파일:

```text
%USERPROFILE%\Documents\ShareNotepad\sharenotepad.log
```

## 핵심 데이터 구조

```cpp
struct DocumentState {
    std::wstring content;
    uint64_t revision;
    bool dirty;
};

enum class OpType {
    Insert,
    Delete,
    SyncRequest,
    SyncResponse
};

struct EditOperation {
    OpType type;
    uint64_t baseRevision;
    uint64_t assignedRevision;
    size_t position;
    std::wstring text;
    size_t length;
    std::string author;
};

struct Session {
    std::string sessionCode;
    std::string hostId;
    std::string peerId;
    DocumentState document;
    uint64_t currentRevision;
};
```



