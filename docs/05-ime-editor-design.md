# IME와 편집 이벤트 설계서

## 목표

ShareNotepad는 한국어 메모장이므로 한글 IME 처리가 중요하다.

한글 조합 중인 미완성 글자를 네트워크로 보내면 상대방 화면에서 글자가 흔들리거나 중복될 수 있다.

## 원칙

- IME 조합 중에는 편집 이벤트를 전송하지 않는다.
- 조합이 확정된 뒤 변경분을 계산해 전송한다.
- 붙여넣기는 하나의 INSERT로 처리한다.
- 원격 변경 적용 중 발생한 Edit change 이벤트는 무시한다.

## 핵심 플래그

```cpp
bool imeComposing = false;
bool remoteApplying = false;
std::wstring lastKnownText;
uint64_t currentRevision = 0;
```

## IME 메시지 처리

관찰할 메시지:

- `WM_IME_STARTCOMPOSITION`
- `WM_IME_COMPOSITION`
- `WM_IME_ENDCOMPOSITION`
- `WM_COMMAND` with `EN_CHANGE`
- `WM_PASTE`

정책:

| 이벤트 | 처리 |
| --- | --- |
| `WM_IME_STARTCOMPOSITION` | `imeComposing = true` |
| `WM_IME_COMPOSITION` | 전송하지 않음 |
| `WM_IME_ENDCOMPOSITION` | `imeComposing = false`, 변경분 계산 |
| `EN_CHANGE` while composing | 무시 |
| `EN_CHANGE` while remoteApplying | 무시 |
| `WM_PASTE` | paste 전후 텍스트 diff로 INSERT 생성 |

## 로컬 변경 감지

MVP에서는 복잡한 편집 엔진을 만들지 않고, Edit control의 전체 텍스트 전후 차이를 비교해 단일 INSERT 또는 DELETE를 계산한다.

예:

```text
old: 안녕
new: 안녕하세요
```

결과:

```text
INSERT pos=2 text=하세요
```

삭제 예:

```text
old: 안녕하세요
new: 안녕
```

결과:

```text
DELETE pos=2 len=3
```

## 단순 diff 알고리즘

1. 앞에서부터 같은 prefix 길이를 찾는다.
2. 뒤에서부터 같은 suffix 길이를 찾는다.
3. old 중간 영역과 new 중간 영역을 비교한다.
4. new 중간만 있으면 INSERT.
5. old 중간만 있으면 DELETE.
6. 둘 다 있으면 DELETE 후 INSERT로 나눈다.

MVP에서는 한 번의 사용자 입력을 최대 2개 operation으로 표현한다.

## 원격 변경 적용

원격 operation을 적용할 때:

```cpp
remoteApplying = true;
ApplyRemoteChange(op);
lastKnownText = GetEditorText();
remoteApplying = false;
```

주의:

- `remoteApplying`이 true일 때 발생하는 `EN_CHANGE`는 전송하지 않는다.
- 적용 후 caret 위치가 크게 튀지 않도록 가능한 기존 caret을 보정한다.

## Caret 보정

원격 INSERT:

- 원격 삽입 위치가 현재 caret 앞이면 caret을 삽입 길이만큼 뒤로 이동한다.
- 원격 삽입 위치가 현재 caret 뒤이면 caret 유지.

원격 DELETE:

- 원격 삭제 영역이 caret 앞이면 caret을 삭제 길이만큼 앞으로 이동한다.
- 원격 삭제 영역이 caret을 포함하면 caret을 삭제 시작 위치로 이동한다.

## 붙여넣기

붙여넣기는 `WM_PASTE` 전후 텍스트 비교로 처리한다.

규칙:

- 붙여넣은 텍스트가 32KB를 넘으면 서버 전송을 거절하고 사용자에게 표시한다.
- 문서 전체 크기가 5MB를 넘으면 적용하지 않는다.

## Undo/Redo

MVP에서는 Win32 Edit control 기본 Undo를 사용한다.

단, 원격 변경이 섞이면 Undo stack이 기대와 달라질 수 있다.

MVP 정책:

- Undo 완전 동기화는 지원하지 않는다.
- 로컬 Undo로 발생한 텍스트 변경은 일반 DELETE/INSERT로 감지해 전송한다.

## 줄바꿈

내부 문서 모델은 `\n`을 기준으로 한다.

Win32 Edit control 표시/입력 시에는 필요에 따라 `\r\n`과 변환한다.

프로토콜 전송:

- UTF-8
- 줄바꿈은 `\n`

파일 저장:

- UTF-8 with no BOM 권장
- 줄바꿈은 Windows 호환을 위해 `\r\n` 저장 가능



