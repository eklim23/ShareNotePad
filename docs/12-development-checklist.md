# ShareNotepad 개발 완료 체크리스트

이 문서는 ShareNotepad MVP를 개발 시작부터 배포 가능한 상태까지 끝내기 위한 체크리스트다.

체크 기준은 "코드가 있다"가 아니라 "직접 실행해서 동작을 확인했다"이다.

## 0. 개발 준비

- [x] Visual Studio 2022 설치 확인
- [x] Windows SDK 설치 확인
- [x] C++ 데스크톱 개발 도구 설치 확인
- [x] x64 Debug 빌드 환경 확인
- [x] x64 Release 빌드 환경 확인
- [x] 프로젝트 폴더 구조 생성
- [x] `client/` 소스 폴더 생성
- [x] `docs/` 문서 기준 최종 확인
- [x] `14-ui-polish-guide.md` UI 폴리시 기준 작성
- [ ] 기본 코딩 규칙 합의
- [x] 로그 저장 위치 확정

완료 기준:

- 빈 Win32 프로그램이 빌드되고 실행된다.
- 실행 후 1초 이내 빈 메인 창이 표시된다.

## 1. Win32 메모장 UI

- [x] `main.cpp` 작성
- [x] Win32 메시지 루프 구현
- [x] 메인 윈도우 생성
- [x] 실행 직후 메모장 편집 화면 표시
- [x] 실제 메모장형 메뉴 바 구현
- [x] Windows 11 메모장형 다크 상단 탭/명령바 UI 1차 구현
- [x] 클래식 Win32 메뉴바 제거 후 커스텀 명령바로 이동
- [x] 다크 편집 영역/상태바 적용
- [x] 용도 불명 아이콘 버튼을 텍스트 버튼으로 정리
- [x] 미구현/불명확한 툴바 버튼 숨김
- [x] 상단 탭을 실제 로컬 탭 전환 기능으로 구현
- [x] `+` 버튼 새 탭 생성 구현
- [x] `Ctrl+T` 새 탭 생성 구현
- [x] 공유 중 탭 전환/새 탭 생성 제한
- [x] 활성/비활성 탭을 탭 박스 형태로 구분
- [x] 창 폭에 따라 탭 폭 자동 조절
- [x] 창 폭이 좁을 때 탭 제목 자동 축약
- [x] 창 폭이 좁을 때 툴바 버튼 단계적 숨김
- [x] DPI awareness 적용으로 고배율 화면 흐림 방지
- [x] `공유 > 방 만들기` 메뉴 구현
- [x] `공유 > 방 들어가기` 메뉴 구현
- [x] `공유 > 초대 코드 복사` 메뉴 구현
- [x] `파일 > 새로 만들기/저장/끝내기` 메뉴 구현
- [x] `편집 > 실행 취소/잘라내기/복사/붙여넣기/삭제/모두 선택` 메뉴 구현
- [x] `보기 > 마크다운 미리보기` 메뉴 구현
- [x] 제목/목록/체크박스/링크/코드블록 기본 미리보기 구현
- [x] `#` + Space 제목 변환 MVP 구현
- [x] `-`/`*` + Space 목록 변환 MVP 구현
- [x] `>` + Space 인용 변환 MVP 구현
- [x] 세 개의 백틱 + Space 코드 스타일 변환 MVP 구현
- [x] Markdown shortcut 마커 제거 방식으로 수정
- [x] 줄바꿈 후 제목/코드 스타일 과도 지속 방지
- [x] 메모장 화면 구현
- [x] multiline Edit control 배치
- [x] 상태바 배치
- [x] 창 크기 변경 시 Edit control/status bar 리사이즈
- [x] 기본 단축키 동작 확인
- [x] 마우스 휠 스크롤 보강
- [x] 프로그램 종료 처리 구현

완료 기준:

- 사용자가 글자를 입력하고 삭제할 수 있다.
- Ctrl+C, Ctrl+V, Ctrl+A가 자연스럽게 동작한다.
- 창 크기를 바꿔도 UI가 깨지지 않는다.

## 1.1 WebView2 UI 전환

- [x] WebView2 전환 설계 문서 작성
- [x] Microsoft WebView2 SDK 패키지 준비
- [x] WebView2 네이티브 창 생성
- [x] HTML/CSS/JS UI 자산 추가
- [x] WebView2 UI 자산 빌드 출력 폴더 복사
- [x] Web UI에서 C++로 `uiReady` 메시지 전달
- [x] C++에서 Web UI로 초기 문서 전달
- [x] Web UI 입력을 C++ 저장 로직에 연결
- [x] 방 만들기/방 들어가기 명령을 C++ 동기화 로직에 연결
- [x] 기존 Win32 UI fallback 정책 결정
- [x] Web UI 탭 전환/새 탭 액션 구현
- [x] Web UI 탭 닫기 액션 구현
- [x] Web UI 파일/편집/보기 메뉴 액션 구현
- [x] Web UI 제목/목록/굵게/기울임/링크/미리보기 버튼 액션 구현
- [x] Web UI Markdown shortcut 보강 (`[]`, `---`, 코드블록)
- [x] 스페이스 입력 시 Markdown 문법을 즉시 블록 스타일로 변환
- [x] Web UI slash command 팔레트 구현
- [x] `/todo`, `/quote`, `/code`, `/div` 명령 구현

완료 기준:

- ShareNotepad 실행 시 WebView2 기반 메모장 화면이 표시된다.
- 저장/공유/동기화 코어가 Web UI와 연결된다.

## 2. 로컬 문서 저장

- [x] 저장 폴더 생성
- [x] `%USERPROFILE%\Documents\ShareNotepad` 경로 처리
- [x] `current.txt` 저장 구현
- [x] UTF-8 저장 구현
- [x] UTF-8 로드 구현
- [x] UTF-8 BOM 로드 대응
- [x] UTF-16 LE BOM 로드 대응
- [x] `current.tmp` 임시 저장 구현
- [x] 원자적 파일 교체 구현
- [x] 앱 시작 시 기존 문서 복구
- [x] 앱 종료 시 즉시 저장
- [x] 저장 실패 로그 기록

완료 기준:

- 프로그램을 껐다 켜도 마지막 문서가 복구된다.
- 한글이 깨지지 않는다.
- 저장 중 실패해도 기존 `current.txt`가 망가지지 않는다.

## 3. 자동 저장

- [x] 편집 발생 시 dirty flag 설정
- [x] autosave timer 구현
- [x] 입력 후 1~3초 내 자동 저장
- [x] 연속 입력 중 저장 과다 발생 방지
- [x] 저장 완료 상태바 표시
- [x] 저장 실패 상태바 표시
- [ ] 1MB 문서 자동 저장 확인
- [ ] 빈 문서 자동 저장 확인

완료 기준:

- 저장 버튼 없이도 문서가 유지된다.
- 빠르게 입력해도 디스크 저장이 과도하게 반복되지 않는다.

## 4. 설정과 로깅

- [x] `config.ini` 저장 구현
- [x] `device_id` 최초 생성
- [x] `device_id` 재실행 후 유지
- [x] 최근 nickname 저장
- [x] 최근 port 저장
- [x] `sharenotepad.log` 생성
- [x] 연결 로그 기록
- [x] 동기화 오류 로그 기록
- [x] 저장 오류 로그 기록
- [x] Debug/Release 로그 수준 분리

완료 기준:

- 재실행해도 장치 식별자와 기본 설정이 유지된다.
- 오류 발생 시 로그로 원인을 추적할 수 있다.

## 5. 프로토콜 기반 구현

- [x] 메시지 구조체 정의
- [x] UTF-16 내부 문자열과 UTF-8 변환 구현
- [x] JSON serialize 구현
- [x] JSON deserialize 구현
- [x] 4바이트 length frame encode 구현
- [x] length frame decode 구현
- [x] 부분 recv 처리
- [x] 여러 메시지가 한 번에 들어오는 경우 처리
- [x] 64KB 초과 메시지 거절
- [x] 깨진 JSON 무시
- [x] 메시지 타입 검증
- [x] 필수 필드 검증

완료 기준:

- `HELLO`, `JOIN`, `INSERT`, `DELETE`, `APPLY`, `SYNC_REQUEST`, `SYNC_RESPONSE`, `PING`, `PONG`, `ERROR`를 주고받을 수 있다.
- TCP 패킷이 쪼개지거나 합쳐져도 메시지를 정상 복원한다.

## 6. TCP 서버

- [x] WinSock 초기화
- [x] 기본 포트 7777 bind
- [x] 7777 사용 중이면 7778~7799 시도
- [x] listen socket 생성
- [x] accept thread 구현
- [x] 클라이언트 1명 접속 수락
- [x] 세션당 최대 2명 제한
- [x] session code 생성
- [x] JOIN 검증
- [x] JOIN_OK 응답
- [x] JOIN_FAIL 응답
- [x] 서버 종료 시 socket 정리

완료 기준:

- 방 만들기를 누르면 로컬 서버가 열린다.
- 연결 주소와 초대 코드가 화면에 표시된다.
- 잘못된 코드로 접속하면 거절된다.

## 7. TCP 클라이언트

- [x] 주소/포트 입력 UI 구현
- [x] 초대 코드만 입력하는 기본 UI 구현
- [x] IP/포트 입력을 고급 옵션으로 이동
- [x] TCP connect 구현
- [x] connect timeout 처리
- [x] HELLO 전송
- [x] JOIN 전송
- [x] JOIN_OK 처리
- [x] JOIN_FAIL 처리
- [x] 수신 thread 구현
- [x] 송신 queue 구현
- [x] 연결 종료 감지
- [x] UI thread로 연결 이벤트 전달

완료 기준:

- 같은 PC에서 두 프로세스를 실행해 `127.0.0.1`로 접속할 수 있다.
- 접속 실패 시 프로그램이 멈추지 않는다.

## 8. 연결 상태 표시

- [x] `Idle` 상태 표시
- [x] `Hosting` 상태 표시
- [x] `Connecting` 상태 표시
- [x] `Syncing` 상태 표시
- [x] `Connected` 상태 표시
- [x] `Reconnecting` 상태 표시
- [x] `Disconnected` 상태 표시
- [ ] 지연 시간 표시
- [x] PING/PONG 구현
- [x] 15초 무응답 감지

## 8.1 자동 연결 UX

- [x] LAN UDP discovery 송신 구현
- [x] LAN UDP discovery 수신 구현
- [x] 방 코드와 discovery session 매칭
- [x] 초대 코드만으로 같은 LAN 자동 접속
- [x] 자동 탐색 실패 시 사용자 친화적 안내
- [x] IP/포트 직접 입력은 고급/디버그 옵션으로만 제공
- [x] 사용자에게 Tailscale/VPN/포트포워딩 요구하지 않음
- [x] 외부망용 rendezvous/relay 설계 문서 작성
- [x] 외부 릴레이 서버 MVP 코드 작성
- [x] 릴레이 서버 배포 문서 작성
- [x] 릴레이 서버 systemd/nginx/caddy 배포 템플릿 작성
- [x] 릴레이 서버 SSH 배포 스크립트 작성
- [x] 릴레이 서버 health check 배포 확인
- [x] WebView 클라이언트 WSS 릴레이 연결 구현
- [x] 릴레이 방 만들기/방 들어가기 UI 연결
- [x] 릴레이 초대 코드로 외부망 접속 확인
- [ ] 릴레이 양방향 입력 반영 확인
- [ ] 릴레이 연결 끊김 상태 표시 확인

완료 기준:

- 사용자는 현재 연결 상태를 상태바만 보고 이해할 수 있다.
- 상대 프로그램 종료 시 끊김 상태가 표시된다.

## 9. 편집 이벤트 감지

- [x] Edit control 현재 텍스트 조회
- [x] `lastKnownText` 관리
- [x] prefix/suffix 기반 단순 diff 구현
- [x] 끝 삽입 감지
- [x] 중간 삽입 감지
- [x] 앞 삽입 감지
- [x] 끝 삭제 감지
- [x] 중간 삭제 감지
- [x] 전체 선택 후 교체 감지
- [x] 붙여넣기 감지
- [x] DELETE 후 INSERT 복합 변경 처리
- [x] 문서 크기 제한 검사

완료 기준:

- 일반 입력, 삭제, 붙여넣기가 INSERT/DELETE operation으로 변환된다.
- 한 번의 변경이 이상한 여러 operation으로 과도하게 쪼개지지 않는다.

## 10. 한글 IME 처리

- [x] `WM_IME_STARTCOMPOSITION` 처리
- [x] `WM_IME_COMPOSITION` 중 전송 억제
- [x] `WM_IME_ENDCOMPOSITION` 처리
- [x] `imeComposing` flag 구현
- [x] 조합 중 `EN_CHANGE` 무시
- [x] 조합 확정 후 diff 계산
- [ ] 받침 있는 글자 입력 테스트
- [ ] 한영 전환 테스트
- [ ] 한글 붙여넣기 테스트
- [ ] 원격 화면 중복 입력 여부 확인

완료 기준:

- 한글 조합 중 미완성 글자가 상대에게 전송되지 않는다.
- `안녕하세요` 같은 문장이 중복 없이 정상 반영된다.

## 11. 동기화 서버 로직

- [x] 서버 DocumentState 보관
- [x] 서버 revision 관리
- [x] INSERT 검증
- [x] DELETE 검증
- [x] 서버 문서에 operation 적용
- [x] revision 증가
- [x] APPLY 메시지 생성
- [x] 모든 peer에 APPLY broadcast
- [x] 잘못된 position 거절
- [x] 문서 크기 초과 거절
- [x] session.log 기록

완료 기준:

- 서버가 모든 변경의 최종 순서를 결정한다.
- 두 클라이언트는 같은 revision과 같은 문서 내용을 가진다.

## 12. 클라이언트 동기화 적용

- [x] APPLY INSERT 적용
- [x] APPLY DELETE 적용
- [x] `remoteApplying` flag 구현
- [x] 원격 적용 중 EN_CHANGE 무시
- [x] 자신의 입력도 APPLY 기준으로 확정
- [x] 이미 적용된 revision 무시
- [x] revision 건너뜀 감지
- [x] revision 불일치 시 SYNC_REQUEST 전송
- [x] 원격 INSERT 시 caret 보정
- [x] 원격 DELETE 시 caret 보정

완료 기준:

- A 입력이 B에 반영된다.
- B 입력이 A에 반영된다.
- 원격 반영 때문에 다시 네트워크 이벤트가 발생하지 않는다.

## 13. 전체 동기화

- [x] SYNC_REQUEST 처리
- [x] SYNC_RESPONSE 생성
- [x] 전체 문서 교체 적용
- [x] 전체 동기화 중 편집 비활성 또는 보수 처리
- [x] 동기화 완료 후 상태 전환
- [x] 1MB 문서 전체 동기화 테스트
- [x] 5MB 제한 테스트

완료 기준:

- 신규 접속자는 접속 직후 최신 전체 문서를 받는다.
- revision 불일치가 전체 동기화로 복구된다.

## 14. 재연결

- [x] reconnect_token 발급
- [x] reconnect_token 저장
- [x] 연결 끊김 감지
- [x] 재연결 backoff 구현
- [x] 재연결 시 JOIN에 token 포함
- [x] 재연결 성공 후 SYNC_REQUEST
- [x] 재연결 실패 시 Disconnected 상태 표시
- [x] 재연결 중 로컬 문서 저장 유지
- [ ] 충돌 시 로컬 백업 생성

완료 기준:

- 일시적으로 네트워크가 끊겨도 프로그램이 죽지 않는다.
- 재접속하면 최신 문서로 복구된다.

## 15. 보안 제한

- [x] 초대 코드 안전 난수 생성
- [ ] 초대 코드 10분 만료
- [x] 세션당 최대 2명 제한
- [x] 잘못된 JOIN 5회 후 차단
- [x] 메시지 최대 64KB 제한
- [x] 단일 INSERT 32KB 제한
- [x] 문서 최대 5MB 제한
- [x] 잘못된 client 무시
- [x] 잘못된 session 거절
- [x] 로그에 reconnect_token 원문 기록 금지
- [x] Relay 서버 기본 localhost 바인딩
- [x] Relay 서버 연결 수/방 수 제한
- [x] Relay 서버 IP 기반 rate limit
- [x] Relay 서버 idle timeout
- [x] Relay 서버 선택적 Origin allowlist
- [x] Relay 서버 방 코드 로그 비노출

완료 기준:

- 비정상 메시지를 받아도 서버가 죽지 않는다.
- MVP 수준에서 무단 접속과 과도한 입력을 막는다.

## 16. 오류 처리

- [ ] 포트 사용 중 처리
- [ ] 주소 입력 오류 처리
- [x] connect timeout 처리
- [x] 상대 종료 처리
- [x] 잘못된 초대 코드 처리
- [x] 방 가득 참 처리
- [x] protocol mismatch 처리
- [x] 저장 실패 처리
- [ ] JSON 파싱 실패 처리
- [ ] UTF-8 변환 실패 처리
- [x] 사용자 친화적 오류 문구 표시

완료 기준:

- 흔한 오류 상황에서 프로그램이 종료되지 않는다.
- 사용자는 다음에 뭘 해야 하는지 대략 이해할 수 있다.

## 17. 내부망 테스트

- [x] 같은 PC에서 두 프로세스 테스트
- [x] `127.0.0.1:7777` 접속 테스트
- [ ] 같은 공유기 아래 두 노트북 테스트
- [ ] 노트북 A 방 만들기 테스트
- [ ] 노트북 B 방 들어가기 테스트
- [x] A -> B 입력 반영 테스트
- [x] B -> A 입력 반영 테스트
- [ ] 삭제 반영 테스트
- [ ] 붙여넣기 반영 테스트
- [ ] 한글 IME 테스트
- [x] 상대 종료 테스트
- [ ] 호스트 종료 테스트
- [ ] 재접속 테스트

완료 기준:

- 실제 두 노트북에서 기본 사용자 시나리오가 끝까지 성공한다.

## 18. 성능 테스트

- [ ] 실행 후 1초 이내 메인 화면 표시
- [ ] 대기 CPU 1% 이하 확인
- [ ] 메모리 사용량 30MB 이하 목표 확인
- [ ] 내부망 입력 반영 100ms 이하 목표 확인
- [ ] 초당 100개 입력 이벤트 처리
- [ ] 1MB 문서 열기
- [x] 1MB 문서 동기화
- [ ] 5MB 문서 제한 동작 확인
- [ ] 10분 이상 연속 입력
- [ ] 장시간 사용 중 메모리 증가 확인

완료 기준:

- MVP 성능 목표를 크게 벗어나지 않는다.
- 장시간 사용해도 메모리가 계속 증가하지 않는다.

## 19. Release 빌드

- [x] x64 Release 빌드 성공
- [x] 배포 폴더 생성 스크립트 작성
- [x] 첫 실행 self-install 흐름 구현
- [x] `%LOCALAPPDATA%\Programs\ShareNotepad` 사용자별 설치 경로 적용
- [x] 시작 메뉴 바로가기 생성 구현
- [x] 앱 아이콘 리소스 적용
- [ ] Debug 전용 로그 제거 또는 축소
- [x] 배포 폴더에서 첫 실행 self-install 확인
- [ ] 새 Windows 사용자 환경에서 실행 확인
- [x] `Documents\ShareNotepad` 자동 생성 확인
- [ ] 방화벽 안내 확인
- [ ] Windows Defender 오탐 여부 확인
- [ ] Release exe로 두 노트북 테스트

완료 기준:

- 개발 환경 밖에서도 `ShareNotepad.exe`가 실행된다.
- Release 빌드로 핵심 시나리오가 성공한다.

## 20. MVP 완료 판정

아래 항목이 모두 체크되어야 MVP 완료로 본다.

- [x] ShareNotepad.exe 실행
- [x] 방 만들기 성공
- [x] 초대 코드 생성
- [ ] 상대 노트북 접속 성공
- [x] 최초 전체 문서 동기화 성공
- [x] A 입력이 B에 반영
- [x] B 입력이 A에 반영
- [ ] 삭제가 양방향 반영
- [ ] 붙여넣기가 양방향 반영
- [ ] 한글 입력이 중복 없이 반영
- [ ] 자동 저장 성공
- [ ] 재실행 후 문서 복구
- [x] 상대 종료 감지
- [x] 재연결 또는 전체 동기화 복구
- [x] 잘못된 초대 코드 거절
- [x] 비정상 메시지로 프로그램이 죽지 않음
- [x] Release 빌드 완료
- [ ] 두 노트북 실사용 테스트 완료

## 21. MVP 이후로 미루는 항목

아래 항목은 MVP 완료 전에는 구현하지 않는다.

- [ ] 계정 로그인
- [ ] 외부 중앙 릴레이 서버
- [ ] TLS
- [ ] OAuth
- [ ] 이메일 매직코드
- [ ] 클라우드 문서 목록
- [ ] 3명 이상 동시 편집
- [ ] CRDT/OT
- [ ] 이미지 첨부
- [ ] 파일 첨부
- [ ] 마크다운 렌더링
- [ ] 다중 문서


