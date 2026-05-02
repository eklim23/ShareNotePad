# ShareNotepad 필요 개발 스킬

이 문서는 ShareNotepad MVP를 끝까지 개발하기 위해 필요한 기술 역량을 정리한다.

목표는 모든 기술을 깊게 공부하는 것이 아니라, MVP 구현에 필요한 만큼 정확히 익히는 것이다.

## 스킬 우선순위

| 우선순위 | 스킬 | 필요한 수준 |
| --- | --- | --- |
| 필수 | C++ Win32 API | 직접 창과 Edit control을 만들 수 있음 |
| 필수 | WinSock TCP | 서버/클라이언트 연결과 송수신 구현 가능 |
| 필수 | C++ 문자열/Unicode | UTF-8, UTF-16, 한글 처리 가능 |
| 필수 | 한글 IME 처리 | 조합 중/확정 후 이벤트 구분 가능 |
| 필수 | 멀티스레딩 | UI thread와 network thread 분리 가능 |
| 필수 | 파일 I/O | 자동 저장과 복구 구현 가능 |
| 필수 | 프로토콜 설계 | length framing과 메시지 검증 가능 |
| 중요 | 네트워크 디버깅 | localhost/LAN 연결 문제 추적 가능 |
| 중요 | 오류 처리 | 프로그램이 죽지 않게 방어 코드 작성 가능 |
| 중요 | 성능 측정 | 메모리, CPU, 지연 시간 확인 가능 |
| 선택 | UDP 브로드캐스트 | 자동 방 탐색 구현 가능 |
| 선택 | TLS/OAuth | 외부망 버전에서 필요 |

## 1. C++ Win32 API

필요 이유:

- Electron이나 브라우저 기반 앱을 쓰지 않기 때문이다.
- 가볍고 빠른 네이티브 메모장 UI가 목표다.

알아야 할 것:

- `WinMain`
- `RegisterClassEx`
- `CreateWindowEx`
- 메시지 루프
- `WndProc`
- Button control
- multiline Edit control
- Status bar
- `WM_SIZE`
- `WM_COMMAND`
- `WM_CLOSE`

적용 위치:

- `main.cpp`
- `window.cpp`
- `editor.cpp`

완료 기준:

- 빈 창을 직접 만들 수 있다.
- 버튼 클릭 이벤트를 받을 수 있다.
- Edit control에 텍스트를 입력하고 읽을 수 있다.
- 창 크기 변경에 맞춰 UI를 다시 배치할 수 있다.

## 2. WinSock TCP 네트워킹

필요 이유:

- 두 노트북 간 실시간 동기화에 필요하다.
- MVP는 내부망 TCP 연결을 기본으로 한다.

알아야 할 것:

- `WSAStartup`
- `socket`
- `bind`
- `listen`
- `accept`
- `connect`
- `send`
- `recv`
- socket close
- timeout 처리
- 부분 송수신 처리

적용 위치:

- `sync_server.cpp`
- `sync_client.cpp`

완료 기준:

- 같은 PC에서 서버와 클라이언트를 연결할 수 있다.
- `127.0.0.1`로 메시지를 주고받을 수 있다.
- 상대가 종료되어도 프로그램이 죽지 않는다.

## 3. C++ 문자열과 Unicode

필요 이유:

- Windows UI는 주로 UTF-16 계열 문자열을 사용한다.
- 네트워크 프로토콜과 파일 저장은 UTF-8을 권장한다.
- 한글이 깨지지 않게 하려면 변환 기준이 명확해야 한다.

알아야 할 것:

- `std::wstring`
- `std::string`
- UTF-8
- UTF-16
- `WideCharToMultiByte`
- `MultiByteToWideChar`
- BOM 처리
- 줄바꿈 변환

적용 위치:

- `protocol.cpp`
- `storage.cpp`
- `editor.cpp`

완료 기준:

- 한글 문서를 저장하고 다시 읽어도 깨지지 않는다.
- 네트워크로 전송한 한글이 상대 화면에 정상 표시된다.

## 4. 한글 IME 처리

필요 이유:

- 한국어 입력은 글자가 조합되는 과정이 있다.
- 조합 중인 미완성 문자를 전송하면 상대 화면에서 중복이나 흔들림이 생긴다.

알아야 할 것:

- `WM_IME_STARTCOMPOSITION`
- `WM_IME_COMPOSITION`
- `WM_IME_ENDCOMPOSITION`
- `EN_CHANGE`
- 조합 중 이벤트 억제
- 조합 확정 후 diff 계산

적용 위치:

- `editor.cpp`

완료 기준:

- `안녕하세요`를 입력해도 상대 화면에 글자가 중복되지 않는다.
- 받침 있는 글자 입력이 정상 반영된다.
- 한글 붙여넣기가 정상 반영된다.

## 5. C++ 멀티스레딩

필요 이유:

- 네트워크 송수신을 UI thread에서 처리하면 창이 멈춘다.
- UI, 네트워크, 저장 흐름을 분리해야 한다.

알아야 할 것:

- `std::thread`
- `std::mutex`
- `std::lock_guard`
- thread 종료 신호
- event queue
- `PostMessage`
- UI thread 안전성

적용 위치:

- `app.cpp`
- `sync_client.cpp`
- `sync_server.cpp`
- `storage.cpp`

완료 기준:

- 네트워크 연결 중에도 UI가 멈추지 않는다.
- worker thread가 종료 시 안전하게 정리된다.
- 네트워크 thread가 직접 Win32 control을 만지지 않는다.

## 6. 파일 I/O와 복구

필요 이유:

- ShareNotepad는 저장 버튼 없이 자동 저장되어야 한다.
- 비정상 종료 후에도 마지막 문서를 복구해야 한다.

알아야 할 것:

- 파일 읽기/쓰기
- 폴더 생성
- 임시 파일 저장
- 원자적 교체
- 백업 파일 관리
- 설정 파일 저장

적용 위치:

- `storage.cpp`
- `config.cpp`

완료 기준:

- 입력 후 1~3초 내 `current.txt`가 저장된다.
- 재실행 시 마지막 문서가 복구된다.
- 저장 실패가 기존 파일 손상으로 이어지지 않는다.

## 7. 프로토콜 설계와 검증

필요 이유:

- TCP는 메시지 경계를 보장하지 않는다.
- 잘못된 메시지 하나로 서버가 죽으면 안 된다.

알아야 할 것:

- length framing
- JSON 메시지 구조
- 필수 필드 검증
- 메시지 크기 제한
- revision 관리
- 전체 동기화 요청/응답

적용 위치:

- `protocol.cpp`
- `sync_server.cpp`
- `sync_client.cpp`

완료 기준:

- 메시지가 쪼개져 들어와도 복원된다.
- 여러 메시지가 한 번에 들어와도 분리된다.
- 깨진 메시지를 받아도 프로그램이 죽지 않는다.

## 8. 편집 diff 계산

필요 이유:

- 전체 문서를 매번 보내면 가벼운 메모장이 아니게 된다.
- 로컬 변경을 INSERT/DELETE operation으로 바꿔야 한다.

알아야 할 것:

- prefix/suffix 비교
- 단일 INSERT 감지
- 단일 DELETE 감지
- 교체를 DELETE + INSERT로 분리
- caret 위치 보정

적용 위치:

- `editor.cpp`

완료 기준:

- 입력, 삭제, 붙여넣기가 operation으로 변환된다.
- 원격 변경 적용 후 caret이 과도하게 튀지 않는다.

## 9. 네트워크 디버깅

필요 이유:

- 내부망 앱은 환경에 따라 접속 실패가 자주 발생한다.
- 방화벽, 포트, AP isolation 문제를 구분해야 한다.

알아야 할 것:

- `127.0.0.1` 테스트
- 같은 PC 두 프로세스 테스트
- LAN IP 확인
- 포트 사용 여부 확인
- Windows 방화벽 확인
- 로그 기반 원인 추적

적용 위치:

- 테스트 단계
- `logger.cpp`

완료 기준:

- localhost에서는 되는데 두 노트북에서는 안 되는 상황을 설명할 수 있다.
- 방화벽/포트/주소 오류를 구분할 수 있다.

## 10. 오류 처리와 방어적 프로그래밍

필요 이유:

- 연결이 끊기거나 잘못된 메시지가 와도 프로그램이 유지되어야 한다.
- 사용자의 문서가 사라지면 안 된다.

알아야 할 것:

- 실패 반환값 확인
- 예외 상황 로그 기록
- 사용자 친화적 오류 문구
- resource cleanup
- 입력 크기 제한
- 상태 머신 기반 처리

적용 위치:

- 전체 모듈

완료 기준:

- 흔한 오류 상황에서 앱이 종료되지 않는다.
- 사용자 문서는 로컬에 남는다.
- 복구 가능한 오류는 전체 동기화로 회복한다.

## 11. 성능 측정

필요 이유:

- ShareNotepad의 핵심 가치는 가벼움과 속도다.
- 기능이 동작해도 무거우면 제품 방향과 맞지 않는다.

알아야 할 것:

- Task Manager로 메모리 확인
- CPU 사용률 확인
- 입력 반영 지연 측정
- 1MB/5MB 문서 테스트
- 장시간 사용 중 메모리 증가 확인

적용 위치:

- Release 전 테스트

완료 기준:

- 실행 시간이 1초 이내에 가깝다.
- 대기 CPU가 1% 이하에 가깝다.
- 내부망 입력 반영이 100ms 이하에 가깝다.

## 12. 선택 스킬: UDP 브로드캐스트

필요 이유:

- 사용자가 IP를 몰라도 방을 찾게 만들 수 있다.

MVP 적용 여부:

- 선택
- 수동 IP 입력을 먼저 구현한다.

완료 기준:

- 같은 LAN에서 방 목록을 자동으로 찾을 수 있다.
- 자동 탐색 실패 시 수동 입력으로 계속 진행할 수 있다.

## 13. 선택 스킬: 외부망 보안

필요 이유:

- 외부망 릴레이 서버 버전에서는 평문 TCP를 쓰면 안 된다.

MVP 적용 여부:

- 제외

나중에 필요한 것:

- TLS
- 세션 토큰
- OAuth 또는 이메일 매직코드
- 서버 로그 최소화
- 평문 문서 저장 금지

## 학습 순서

처음부터 모든 걸 배우려고 하지 말고 아래 순서로 진행한다.

1. Win32 창과 Edit control
2. 파일 저장/로드
3. 자동 저장 timer
4. WinSock localhost 연결
5. length framing
6. JSON 메시지
7. INSERT/DELETE diff
8. 서버 revision
9. 한글 IME 처리
10. 재연결과 전체 동기화
11. 오류 처리
12. 성능 측정

## MVP 개발 가능 판정

아래 질문에 모두 "예"라고 답할 수 있으면 MVP 구현을 시작해도 된다.

- [ ] Win32 창을 직접 만들 수 있는가?
- [ ] Edit control에서 텍스트를 읽고 쓸 수 있는가?
- [ ] 파일에 한글 텍스트를 저장하고 다시 읽을 수 있는가?
- [ ] TCP 서버와 클라이언트를 localhost에서 연결할 수 있는가?
- [ ] TCP 메시지를 length frame으로 나눠 처리할 수 있는가?
- [ ] 한글 IME 조합 중/확정 후를 구분할 수 있는가?
- [ ] UI thread와 network thread를 분리할 수 있는가?
- [ ] 오류가 나도 프로그램을 종료하지 않고 상태로 처리할 수 있는가?



