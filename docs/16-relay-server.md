# Relay Server 설계

학교/회사/공용 Wi-Fi에서는 같은 네트워크에 있어도 기기 간 직접 통신이 막힐 수 있다.
ShareNotepad Relay Server는 이런 환경에서 두 클라이언트가 모두 외부 서버로 나가는 WebSocket 연결을 만들고, 서버가 메시지만 중계하도록 하는 구성이다.

## 목표

- 사용자는 계속 방 코드만 입력한다.
- 클라이언트 간 직접 접속이 막혀도 동작한다.
- 서버는 문서를 저장하지 않는다.
- MVP에서는 DB 없이 메모리 방 관리만 사용한다.

## 구조

```text
Laptop A ── WSS /ws ── relay-server ── WSS /ws ── Laptop B
```

## 서버 역할

- 추측하기 어려운 alphanumeric 방 코드 생성
- 방당 최대 2명 접속
- `A -> B`, `B -> A` JSON payload 중계
- 빈 방 삭제
- 비활성 방 만료
- `/healthz` 상태 확인

## 배포 권장

동아리 서버 기준:

```text
relay-server : localhost:8080
nginx/caddy  : 443 TLS 종료
public URL   : wss://도메인/ws
```

앱에는 최종적으로 아래 주소만 설정하면 된다.

```text
wss://도메인/ws
```

## 클라이언트 fallback 순서

1. 내부망 자동 탐색
2. 내부망 직접 연결
3. Relay Server 연결

학교망에서는 1, 2가 막힐 가능성이 높으므로 3번이 실사용 경로가 된다.
