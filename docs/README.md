# ShareNotepad 개발 문서

이 폴더는 ShareNotepad MVP 구현을 위한 기준 문서 모음이다.

ShareNotepad의 우선순위는 다음 순서로 고정한다.

1. 가벼움
2. 속도
3. 안정성
4. 기능 다양성

MVP는 로그인 없는 2인 내부망 실시간 공유 메모장을 목표로 한다.

## 문서 목록

| 문서 | 목적 |
| --- | --- |
| [00-mvp-scope.md](00-mvp-scope.md) | MVP 범위와 제외 기능 확정 |
| [01-protocol.md](01-protocol.md) | TCP 메시지 프레이밍, 메시지 타입, 검증 규칙 |
| [02-ui-flow.md](02-ui-flow.md) | 화면 흐름과 사용자 상태 |
| [03-module-design.md](03-module-design.md) | C++ 모듈 구조와 책임 |
| [04-threading-and-state.md](04-threading-and-state.md) | 스레드 구조, 상태 머신, 락 정책 |
| [05-ime-editor-design.md](05-ime-editor-design.md) | 한글 IME, 편집 이벤트, 원격 반영 정책 |
| [06-storage-recovery.md](06-storage-recovery.md) | 자동 저장, 백업, 비정상 종료 복구 |
| [07-networking.md](07-networking.md) | 내부망 연결, 방 생성/입장, 재연결 |
| [08-session-auth-security.md](08-session-auth-security.md) | 로그인 없는 세션 인증과 보안 제한 |
| [09-error-scenarios.md](09-error-scenarios.md) | 오류 상황별 처리 정책 |
| [10-build-distribution.md](10-build-distribution.md) | 빌드, 배포, 실행 환경 |
| [11-test-plan.md](11-test-plan.md) | 단위/통합/성능 테스트 계획 |
| [12-development-checklist.md](12-development-checklist.md) | 개발 시작부터 MVP 완료까지 체크리스트 |
| [13-required-skills.md](13-required-skills.md) | ShareNotepad 개발에 필요한 기술 역량과 학습 순서 |
| [14-ui-polish-guide.md](14-ui-polish-guide.md) | Windows 11 메모장형 UI 다듬기 기준 |
| [15-webview2-transition.md](15-webview2-transition.md) | WebView2 UI 전환 구조와 브릿지 기준 |
| [16-relay-server.md](16-relay-server.md) | 학교망 대응용 외부 릴레이 서버 설계 |

## 개발 시작 순서

1. `00-mvp-scope.md`로 구현 범위를 고정한다.
2. `01-protocol.md`와 `04-threading-and-state.md`를 기준으로 네트워크 골격을 만든다.
3. `05-ime-editor-design.md`를 기준으로 Win32 Edit control 이벤트 처리를 구현한다.
4. `06-storage-recovery.md`로 자동 저장을 먼저 붙인다.
5. 이후 방 만들기, 방 들어가기, 실시간 동기화를 붙인다.


