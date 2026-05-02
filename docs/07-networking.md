# 네트워크 설계서

## 목표

사용자는 초대 코드만 입력한다.

같은 LAN에서는 별도 서버 없이 두 노트북을 자동으로 연결한다.

서로 다른 네트워크에서는 사용자가 VPN이나 Tailscale을 직접 설정하지 않도록, ShareNotepad가 관리하는 rendezvous/relay 서버를 사용한다.

노트북 A는 UI와 미니 서버를 함께 실행한다.

```text
[노트북 A]
 ├─ ShareNotepad UI
 └─ Local Sync Server

[노트북 B]
 └─ ShareNotepad UI
```

## 연결 UX 원칙

기본 화면에는 IP와 포트를 노출하지 않는다.

기본 흐름:

```text
초대 코드 입력 -> 자동 검색 -> 자동 접속
```

고급/디버그 옵션에서만 IP와 포트 직접 입력을 제공한다.

사용자에게 요구하지 않는 것:

- Tailscale 설치
- ZeroTier 설치
- VPN 연결
- 포트 포워딩
- 공유기 설정
- 상대 IP 확인

개발 우선순위:

1. `127.0.0.1` 단일 PC 테스트
2. 같은 PC에서 두 프로세스 테스트
3. UDP discovery로 같은 LAN 자동 탐색
4. 같은 LAN의 두 노트북에서 초대 코드만으로 접속
5. 앱 관리 rendezvous/relay 서버 추가

## 포트

기본 포트:

```text
7777
```

포트 사용 중이면:

1. 7777 시도
2. 7778~7799 순차 시도
3. 성공한 포트를 UI에 표시

## 방 만들기 흐름

```text
사용자: 방 만들기 클릭
 -> session_code 생성
 -> TCP listen socket 생성
 -> port bind/listen
 -> server thread 시작
 -> 방 생성 화면 표시
```

초대 정보:

```text
초대 코드: 482913
```

연결 주소와 포트는 고급/디버그 정보로만 표시한다.

## 방 들어가기 흐름

```text
사용자: 초대 코드 입력
 -> LAN UDP discovery 시도
 -> 발견되면 TCP connect
 -> 발견되지 않으면 rendezvous/relay 조회
 -> HELLO
 -> JOIN
 -> JOIN_OK
 -> SYNC_REQUEST
 -> SYNC_RESPONSE
 -> Connected
```

## 브로드캐스트 탐색

초대 코드만으로 같은 LAN의 호스트를 찾기 위해 UDP discovery를 사용한다.

호스트가 1초마다 전송:

```text
SHARENOTEPAD_DISCOVERY v=1 session=482913 host=192.168.0.15 port=7777
```

참여자는 사용자가 입력한 방 코드와 discovery 메시지의 session이 일치하면 자동으로 TCP 접속한다.

주의:

- 학교망/공용망에서는 AP isolation 때문에 막힐 수 있다.
- 자동 탐색 실패가 사용자 잘못은 아니다.
- 이 경우 앱 관리 rendezvous/relay 서버로 넘어간다.
- IP 직접 입력은 고급/디버그 옵션으로만 둔다.

## Rendezvous/Relay

서로 다른 네트워크에서 방 코드만으로 연결하려면 앱이 관리하는 서버가 필요하다.

역할:

```text
방 코드 -> 호스트 접속 후보 정보
직접 연결 실패 -> relay 중계
```

원칙:

- 사용자에게 별도 설치나 네트워크 설정을 요구하지 않는다.
- 문서 내용을 영구 저장하지 않는다.
- 로그인 없이도 일회성 방 코드로 접속 가능해야 한다.
- TLS는 외부망 단계에서 필수다.

MVP 내부망 구현 중에는 서버를 필수로 만들지 않지만, 제품 UX 기준으로는 외부망 자동 연결의 정식 해법이다.

## 연결 유지

PING/PONG:

- 5초마다 PING
- 15초 무응답이면 연결 끊김
- 30초 이상 복구 실패 시 Disconnected 표시

## 재연결

재연결 시 클라이언트는 기존 `reconnect_token`을 보낸다.

```json
{
  "type": "JOIN",
  "session": "482913",
  "client": "B",
  "join_code": "482913",
  "reconnect_token": "7f991f2d4a0e4e5b9e2f"
}
```

서버는 토큰이 유효하면 같은 peer로 인정한다.

재연결 성공 후에는 반드시 전체 문서 동기화를 수행한다.

## 서버 제한

MVP 서버 제한:

- 세션당 최대 2명
- 접속 대기 시간 10분
- 잘못된 JOIN 5회 후 연결 종료
- 메시지 최대 64KB
- 문서 최대 5MB

## 방화벽

Windows에서 첫 실행 시 방화벽 허용 팝업이 나올 수 있다.

UI 문구:

```text
상대 노트북이 같은 네트워크에서 접속하려면 Windows 방화벽 허용이 필요할 수 있습니다.
```

MVP에서는 방화벽을 자동으로 수정하지 않는다.


