# ShareNotepad Relay Server

This is the external relay server for school/company networks where devices on the same Wi-Fi cannot connect to each other directly.

The server does not store documents. It only keeps short-lived rooms in memory and relays JSON messages between two connected peers.

## Run

```bash
go run .
```

Default address:

```text
127.0.0.1:8080
```

Environment variables:

| Name | Default | Description |
| --- | --- | --- |
| `SHARENOTEPAD_RELAY_ADDR` | `127.0.0.1:8080` | HTTP/WebSocket listen address |
| `SHARENOTEPAD_ROOM_TTL` | `30m` | Inactive room expiration |
| `SHARENOTEPAD_IDLE_TIMEOUT` | `2m` | WebSocket idle timeout |
| `SHARENOTEPAD_MAX_MESSAGE_BYTES` | `65536` | Maximum WebSocket message size |
| `SHARENOTEPAD_MAX_ROOMS` | `1000` | Maximum active rooms |
| `SHARENOTEPAD_MAX_CONNECTIONS` | `2000` | Maximum active WebSocket connections |
| `SHARENOTEPAD_RATE_LIMIT_PER_MINUTE` | `60` | Per-IP create/join/connect action limit |
| `SHARENOTEPAD_ROOM_CODE_LENGTH` | `8` | Room code length |
| `SHARENOTEPAD_ALLOWED_ORIGINS` | empty | Optional comma-separated WebSocket Origin allowlist |

Short aliases `ADDR`, `ROOM_TTL`, and `MAX_MESSAGE_BYTES` are also supported for quick local testing.

Example:

```bash
SHARENOTEPAD_RELAY_ADDR=127.0.0.1:8080 SHARENOTEPAD_ROOM_TTL=30m go run .
```

Build:

```bash
go build -o sharenotepad-relay .
./sharenotepad-relay
```

## Install On Linux

```bash
chmod +x scripts/install-linux.sh
./scripts/install-linux.sh
```

The script builds the server, installs it to `/opt/sharenotepad-relay`, creates `/etc/sharenotepad-relay.env`, and starts a systemd service.

## Docker

```bash
docker compose up -d --build
```

## Endpoints

```text
GET /healthz
GET /ws
```

In production, put this server behind Nginx/Caddy and expose it as HTTPS/WSS:

```text
wss://your-domain.example/ws
```

## Client Protocol

Create a room:

```json
{"type":"CREATE"}
```

Server response:

```json
{"type":"ROOM_CREATED","room":"482913","peer":"A","ttl_seconds":1800}
```

Join a room:

```json
{"type":"JOIN","room":"482913"}
```

Server response:

```json
{"type":"JOIN_OK","room":"482913","peer":"B","ttl_seconds":1800}
```

Relay a ShareNotepad sync payload:

```json
{
  "type": "RELAY",
  "payload": {
    "type": "INSERT",
    "base_rev": 1,
    "pos": 0,
    "text": "hello"
  }
}
```

Peer receives:

```json
{
  "type": "RELAY",
  "room": "482913",
  "from": "A",
  "payload": {
    "type": "INSERT",
    "base_rev": 1,
    "pos": 0,
    "text": "hello"
  }
}
```

The server also accepts unknown JSON message types after a peer joins a room and wraps them as `RELAY` payloads for the other peer.

## Notes

- Maximum 2 peers per room.
- Rooms use random non-ambiguous alphanumeric codes.
- Rooms are deleted when empty.
- Inactive rooms expire automatically.
- No database is required for MVP.
- For public deployment, expose only HTTPS/WSS through Nginx or Caddy.
