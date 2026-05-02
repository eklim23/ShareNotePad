#!/usr/bin/env bash
set -euo pipefail

APP_NAME="sharenotepad-relay"
INSTALL_DIR="/opt/sharenotepad-relay"
ENV_FILE="/etc/sharenotepad-relay.env"
SERVICE_FILE="/etc/systemd/system/sharenotepad-relay.service"

cd "$(dirname "$0")/.."

if ! command -v go >/dev/null 2>&1; then
  echo "Go is not installed. Install Go 1.22+ first, then run this script again." >&2
  exit 1
fi

echo "Building ${APP_NAME}..."
go build -trimpath -ldflags="-s -w" -o "${APP_NAME}" .

echo "Installing to ${INSTALL_DIR}..."
sudo install -d -m 0755 "${INSTALL_DIR}"
sudo install -m 0755 "${APP_NAME}" "${INSTALL_DIR}/${APP_NAME}"

if [ ! -f "${ENV_FILE}" ]; then
  echo "Creating ${ENV_FILE}..."
  sudo tee "${ENV_FILE}" >/dev/null <<'EOF'
SHARENOTEPAD_RELAY_ADDR=127.0.0.1:8080
SHARENOTEPAD_ROOM_TTL=30m
SHARENOTEPAD_IDLE_TIMEOUT=2m
SHARENOTEPAD_MAX_MESSAGE_BYTES=65536
SHARENOTEPAD_MAX_ROOMS=1000
SHARENOTEPAD_MAX_CONNECTIONS=2000
SHARENOTEPAD_RATE_LIMIT_PER_MINUTE=60
SHARENOTEPAD_ROOM_CODE_LENGTH=8
# Example: SHARENOTEPAD_ALLOWED_ORIGINS=https://relay.example.com,https://app.example.com
EOF
fi

echo "Installing systemd service..."
sudo install -m 0644 deploy/systemd/sharenotepad-relay.service "${SERVICE_FILE}"
sudo systemctl daemon-reload
sudo systemctl enable --now sharenotepad-relay

echo "Service status:"
sudo systemctl --no-pager --full status sharenotepad-relay || true

echo "Health check:"
if command -v curl >/dev/null 2>&1; then
  curl -fsS http://127.0.0.1:8080/healthz
  echo
else
  echo "curl is not installed; skip local health check."
fi

echo "Done."
