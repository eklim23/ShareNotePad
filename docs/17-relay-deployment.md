# Relay Server 배포 문서

이 문서는 동아리 Linux 서버에 ShareNotepad Relay Server를 올리는 절차다.

## 권장 구조

```text
client app
  └─ wss://relay.example.com/ws

nginx 또는 caddy
  └─ 127.0.0.1:8080

sharenotepad-relay systemd service
```

## 서버 요구사항

- Linux 서버
- Go 1.22+
- systemd
- 8080 로컬 포트 사용 가능
- 외부 공개는 443 HTTPS/WSS 권장

## 빠른 설치

로컬 PC에서 SSH 접속 별칭이 잡혀 있다면:

```powershell
.\scripts\deploy-relay.ps1 -HostName club-server
```

서버 안에서 직접 설치한다면:

```bash
git clone https://github.com/eklim23/ShareNotePad.git
cd ShareNotePad/relay-server
chmod +x scripts/install-linux.sh
./scripts/install-linux.sh
```

설치 후 확인:

```bash
systemctl status sharenotepad-relay
curl http://127.0.0.1:8080/healthz
```

## Nginx 예시

템플릿:

```text
relay-server/deploy/nginx/sharenotepad-relay.conf
```

설치 예:

```bash
sudo cp relay-server/deploy/nginx/sharenotepad-relay.conf /etc/nginx/sites-available/sharenotepad-relay
sudo ln -s /etc/nginx/sites-available/sharenotepad-relay /etc/nginx/sites-enabled/sharenotepad-relay
sudo nginx -t
sudo systemctl reload nginx
```

TLS는 Certbot 등을 사용한다.

## Caddy 예시

템플릿:

```text
relay-server/deploy/caddy/Caddyfile
```

Caddy는 도메인만 연결되어 있으면 HTTPS 인증서를 자동 발급한다.

## Docker 방식

Docker가 있는 서버라면:

```bash
cd relay-server
docker compose up -d --build
```

이 경우에도 외부 공개는 nginx/caddy reverse proxy로 443을 붙이는 것을 권장한다.

## 환경 변수

```text
SHARENOTEPAD_RELAY_ADDR=127.0.0.1:8080
SHARENOTEPAD_ROOM_TTL=30m
SHARENOTEPAD_IDLE_TIMEOUT=2m
SHARENOTEPAD_MAX_MESSAGE_BYTES=65536
SHARENOTEPAD_MAX_ROOMS=1000
SHARENOTEPAD_MAX_CONNECTIONS=2000
SHARENOTEPAD_RATE_LIMIT_PER_MINUTE=60
SHARENOTEPAD_ROOM_CODE_LENGTH=8
SHARENOTEPAD_ALLOWED_ORIGINS=
```

systemd 설치 방식에서는 `/etc/sharenotepad-relay.env`에 저장된다.

공개 서버에서는 `SHARENOTEPAD_RELAY_ADDR=127.0.0.1:8080`으로 두고 nginx/caddy만 외부에 노출하는 것을 권장한다.
브라우저 기반 클라이언트를 붙일 경우 `SHARENOTEPAD_ALLOWED_ORIGINS=https://도메인`을 설정한다.
