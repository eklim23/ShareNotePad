# 저장과 복구 설계서

## 목표

사용자가 저장 버튼을 누르지 않아도 현재 문서가 유지되어야 한다.

비정상 종료 후 재실행하면 가능한 마지막 상태로 복구한다.

## 저장 위치

```text
%USERPROFILE%\Documents\ShareNotepad
 ├─ current.txt
 ├─ current.tmp
 ├─ backup_YYYYMMDD_HHMMSS.txt
 ├─ session.log
 ├─ config.ini
 └─ sharenotepad.log
```

## 저장 파일

| 파일 | 역할 |
| --- | --- |
| `current.txt` | 마지막 정상 저장 문서 |
| `current.tmp` | 저장 중 임시 파일 |
| `backup_*.txt` | 주기적 백업 |
| `session.log` | 적용된 operation 기록 |
| `config.ini` | device_id, nickname, port 등 |
| `sharenotepad.log` | 디버깅 로그 |

## 자동 저장 정책

입력 발생:

```text
문서 변경
 -> dirty = true
 -> autosave timer 예약
 -> 1~3초 동안 추가 입력 없으면 저장
 -> dirty = false
```

권장 값:

- debounce: 1500ms
- 최대 지연: 3000ms
- 종료 시 즉시 저장

## 원자적 저장

`current.txt`를 직접 덮어쓰지 않는다.

순서:

1. `current.tmp`에 전체 문서 저장
2. flush
3. 기존 `current.txt`를 교체
4. 성공 시 `current.tmp` 삭제

Windows 구현 후보:

- `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`
- `ReplaceFile`

## 백업 정책

MVP 권장:

- 앱 시작 시 기존 `current.txt`를 백업
- 10분 이상 편집 중이면 새 백업 생성
- 백업 파일은 최근 20개만 유지

파일명:

```text
backup_20260429_113000.txt
```

## session.log

operation 로그 형식은 JSON Lines를 사용한다.

예:

```json
{"rev":106,"op":"INSERT","author":"A","pos":12,"text":"안녕하세요"}
{"rev":107,"op":"DELETE","author":"B","pos":5,"len":3}
```

목적:

- 디버깅
- 비정상 종료 후 복구 보조
- revision 추적

MVP에서는 `session.log`가 깨져도 `current.txt`를 우선한다.

## 시작 시 복구 순서

1. 저장 폴더 생성
2. `current.tmp` 존재 확인
3. `current.txt` 존재 확인
4. 더 안전한 파일 선택
5. 텍스트 로드
6. 문서 revision 초기화

복구 우선순위:

| 상황 | 처리 |
| --- | --- |
| `current.txt`만 있음 | 로드 |
| `current.tmp`만 있음 | 사용자 확인 없이 임시 파일 로드 후 저장 |
| 둘 다 있음 | `current.txt` 우선, tmp는 백업으로 보관 |
| 둘 다 없음 | 빈 문서 시작 |

## 인코딩

저장 인코딩:

- UTF-8 권장

읽기 시 허용:

- UTF-8
- UTF-8 BOM
- UTF-16 LE BOM

내부 표현:

- `std::wstring`

프로토콜:

- UTF-8

## 문서 크기 제한

MVP 제한:

- 최대 문서 크기: 5MB
- 단일 메시지: 64KB
- 단일 INSERT text: 32KB

제한 초과 시:

- 로컬 적용 전이면 거절
- 이미 로컬 적용된 경우 상태바에 표시하고 원격 전송은 막는다



