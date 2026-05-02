# 동기화 프로토콜 명세서

## 목표

프로토콜은 가볍고 디버깅하기 쉬워야 한다.

MVP에서는 TCP 위에 길이 프레이밍된 UTF-8 JSON 메시지를 사용한다.

## 전송 규칙

TCP는 메시지 경계를 보장하지 않으므로 모든 메시지는 길이 프레이밍을 사용한다.

프레임 구조:

```text
[4 bytes length, big-endian][UTF-8 JSON payload]
```

규칙:

- `length`는 JSON payload의 바이트 길이다.
- `length` 최대값은 64KB이다.
- payload는 UTF-8 JSON이어야 한다.
- 잘못된 length, 깨진 UTF-8, 깨진 JSON은 즉시 무시하고 오류 카운트를 올린다.
- 오류 카운트가 5회를 넘으면 연결을 종료한다.

## 공통 필드

모든 메시지는 아래 필드를 가진다.

```json
{
  "type": "PING",
  "session": "482913",
  "client": "A"
}
```

| 필드 | 타입 | 설명 |
| --- | --- | --- |
| `type` | string | 메시지 타입 |
| `session` | string | 6자리 방 코드 |
| `client` | string | `A` 또는 `B` |

## 메시지 타입

| 타입 | 방향 | 설명 |
| --- | --- | --- |
| `HELLO` | client -> server | 접속 시작 |
| `JOIN` | client -> server | 방 입장 요청 |
| `JOIN_OK` | server -> client | 입장 성공 |
| `JOIN_FAIL` | server -> client | 입장 실패 |
| `INSERT` | client -> server | 로컬 삽입 요청 |
| `DELETE` | client -> server | 로컬 삭제 요청 |
| `APPLY` | server -> client | 서버가 확정한 변경 |
| `SYNC_REQUEST` | client -> server | 전체 문서 요청 |
| `SYNC_RESPONSE` | server -> client | 전체 문서 응답 |
| `PING` | both | 연결 확인 |
| `PONG` | both | 연결 확인 응답 |
| `ERROR` | server -> client | 처리 가능한 오류 |

## HELLO

클라이언트가 서버에 최초로 보내는 메시지다.

```json
{
  "type": "HELLO",
  "session": "482913",
  "client": "B",
  "protocol": 1,
  "device_id": "b9f8f6c0-8ac0-4a68-9b63-5d928c0c7a11",
  "nickname": "Laptop B"
}
```

검증:

- `protocol`이 현재 지원 버전이어야 한다.
- `device_id`는 비어 있으면 안 된다.
- `nickname`은 32자 이하이다.

## JOIN

방 입장 요청이다.

```json
{
  "type": "JOIN",
  "session": "482913",
  "client": "B",
  "join_code": "482913",
  "reconnect_token": ""
}
```

신규 접속이면 `reconnect_token`은 빈 문자열이다.

재접속이면 기존 토큰을 보낸다.

## JOIN_OK

```json
{
  "type": "JOIN_OK",
  "session": "482913",
  "client": "server",
  "assigned_client": "B",
  "reconnect_token": "7f991f2d4a0e4e5b9e2f",
  "current_rev": 120
}
```

클라이언트는 `JOIN_OK`를 받으면 즉시 `SYNC_REQUEST`를 보낸다.

## JOIN_FAIL

```json
{
  "type": "JOIN_FAIL",
  "session": "482913",
  "client": "server",
  "reason": "invalid_code"
}
```

가능한 `reason`:

- `invalid_code`
- `room_full`
- `expired_code`
- `protocol_mismatch`
- `blocked`

## INSERT

```json
{
  "type": "INSERT",
  "session": "482913",
  "client": "A",
  "base_rev": 104,
  "pos": 12,
  "text": "안녕하세요"
}
```

검증:

- `base_rev`는 숫자여야 한다.
- `pos`는 현재 문서 길이 이하이다.
- `text`는 비어 있으면 안 된다.
- `text`는 UTF-8 기준 32KB 이하이다.
- 적용 후 문서 크기가 5MB를 넘으면 거절한다.

## DELETE

```json
{
  "type": "DELETE",
  "session": "482913",
  "client": "B",
  "base_rev": 105,
  "pos": 5,
  "len": 3
}
```

검증:

- `pos + len`이 현재 문서 길이 이하여야 한다.
- `len`은 1 이상이어야 한다.

## APPLY

서버가 최종 순서를 부여한 변경 메시지다.

```json
{
  "type": "APPLY",
  "session": "482913",
  "client": "server",
  "rev": 106,
  "op": "INSERT",
  "author": "A",
  "pos": 12,
  "text": "안녕하세요"
}
```

삭제 적용 예:

```json
{
  "type": "APPLY",
  "session": "482913",
  "client": "server",
  "rev": 107,
  "op": "DELETE",
  "author": "B",
  "pos": 5,
  "len": 3
}
```

클라이언트 규칙:

- `rev`가 현재 revision + 1이면 적용한다.
- 이미 적용한 `rev`는 무시한다.
- `rev`가 건너뛰면 `SYNC_REQUEST`를 보낸다.
- 자신의 입력도 서버 `APPLY`를 기준으로 확정한다.

## SYNC_REQUEST

```json
{
  "type": "SYNC_REQUEST",
  "session": "482913",
  "client": "B",
  "known_rev": 91
}
```

## SYNC_RESPONSE

```json
{
  "type": "SYNC_RESPONSE",
  "session": "482913",
  "client": "server",
  "rev": 120,
  "chunk_index": 0,
  "chunk_count": 3,
  "text": "현재 전체 문서 내용의 일부"
}
```

규칙:

- `SYNC_RESPONSE`는 64KB 프레임 제한을 지키기 위해 chunk 단위로 전송한다.
- `chunk_index`는 0부터 시작한다.
- `chunk_count`는 같은 revision 전체 chunk 수이다.
- 클라이언트는 같은 `rev`의 모든 chunk를 모은 뒤 `text`를 순서대로 이어 전체 문서를 교체한다.
- 전체 문서 크기는 MVP 기준 5MB 이하이다.
- 클라이언트는 `remoteApplying` 상태에서 전체 문서를 교체한다.

## PING/PONG

```json
{
  "type": "PING",
  "session": "482913",
  "client": "A",
  "time": 1777440000000
}
```

```json
{
  "type": "PONG",
  "session": "482913",
  "client": "B",
  "time": 1777440000000
}
```

규칙:

- 5초마다 PING을 보낸다.
- 15초 동안 응답이 없으면 연결 끊김으로 판단한다.

## ERROR

```json
{
  "type": "ERROR",
  "session": "482913",
  "client": "server",
  "code": "bad_position",
  "message": "position is outside document range"
}
```

클라이언트는 `ERROR` 수신 시 상태바에 간단히 표시하고 필요하면 `SYNC_REQUEST`를 보낸다.

## Revision 정책

- 서버만 revision을 증가시킨다.
- 클라이언트는 로컬 입력 발생 시 서버에 요청을 보낸다.
- 최종 문서 변경은 서버의 `APPLY` 순서를 따른다.
- revision 불일치가 감지되면 전체 동기화로 복구한다.


