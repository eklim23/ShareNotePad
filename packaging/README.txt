ShareNotepad
============

실행 방법
1. 이 폴더를 원하는 위치에 둡니다.
2. ShareNotepad.exe를 실행합니다.
3. 첫 실행 시 앱이 사용자별 설치 경로에 자동 설치되고, 설치된 앱이 다시 실행됩니다.
4. 방 만들기 또는 방 들어가기로 같은 와이파이의 다른 사용자와 공유합니다.

포함 파일
- ShareNotepad.exe
- WebView2Loader.dll
- ui\
- README.txt
- VERSION.txt
- ShareNotepad.portable

설치 위치
- 앱 파일은 첫 실행 시 아래 위치로 복사됩니다.
- %LOCALAPPDATA%\Programs\ShareNotepad
- 시작 메뉴에 ShareNotepad 바로가기가 생성됩니다.

저장 위치
- 자동 저장 파일은 사용자 문서 폴더 아래에 생성됩니다.
- %USERPROFILE%\Documents\ShareNotepad\current.txt

주의 사항
- WebView2 Runtime이 없는 PC에서는 실행되지 않을 수 있습니다.
- Windows 10/11 최신 환경에는 보통 이미 설치되어 있습니다.
- 방 만들기를 사용할 때 Windows 방화벽 허용 팝업이 뜨면 허용해야 같은 와이파이의 상대가 접속할 수 있습니다.
- 현재 배포 폴더에는 개발용 테스트 exe, 라이브러리 파일, WebView2 캐시 폴더를 포함하지 않습니다.
- 다운로드 폴더를 삭제해도 설치된 앱과 문서 파일은 유지됩니다.
