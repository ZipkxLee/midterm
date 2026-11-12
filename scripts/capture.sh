#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
PORT="${CAPTURE_PORT:-25060}"
mkdir -p artifacts

if ! command -v tcpdump >/dev/null 2>&1; then
    echo "tcpdump is not installed. On Ubuntu/WSL run: sudo apt update && sudo apt install tcpdump"
    exit 2
fi
if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required to capture loopback packets; run this script as root instead."
    exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
    echo "GNU timeout is required to stop tcpdump cleanly."
    exit 2
fi

sudo -v
CAPTURE_FILE="$ROOT_DIR/artifacts/client-server.pcap"
CAPTURE_PID=""
SERVER_PID=""
cleanup() {
    if [[ -n "${SERVER_PID:-}" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    if [[ -n "${CAPTURE_PID:-}" ]]; then
        wait "$CAPTURE_PID" 2>/dev/null || true
        CAPTURE_PID=""
    fi
}
trap cleanup EXIT

sudo timeout --signal=INT --kill-after=3s 8s tcpdump -i lo -U -nn -s 0 -w "$CAPTURE_FILE" "tcp port $PORT" \
    >"$ROOT_DIR/artifacts/tcpdump.log" 2>&1 &
CAPTURE_PID=$!
capture_ready=0
for attempt in $(seq 1 200); do
    if grep -q '^tcpdump: listening on' "$ROOT_DIR/artifacts/tcpdump.log"; then
        capture_ready=1
        break
    fi
    if ! kill -0 "$CAPTURE_PID" 2>/dev/null; then
        break
    fi
done
if [[ "$capture_ready" != 1 ]]; then
    echo "tcpdump did not become ready; inspect artifacts/tcpdump.log" >&2
    exit 1
fi

./build/server 127.0.0.1 "$PORT" >"$ROOT_DIR/artifacts/server-capture.log" 2>&1 &
SERVER_PID=$!

ready=0
for attempt in $(seq 1 200); do
    if ./build/client 127.0.0.1 "$PORT" PING >/dev/null 2>&1; then
        ready=1
        break
    fi
done
if [[ "$ready" != 1 ]]; then
    echo "Server did not become ready; inspect artifacts/server-capture.log" >&2
    exit 1
fi
./build/client 127.0.0.1 "$PORT" INFO | tee "$ROOT_DIR/artifacts/capture-client-output.txt"
cleanup
trap - EXIT
if [[ ! -s "$CAPTURE_FILE" ]] || (( $(stat -c '%s' "$CAPTURE_FILE") <= 24 )); then
    echo "Capture contains no packets; inspect artifacts/tcpdump.log" >&2
    exit 1
fi
tcpdump -nn -r "$CAPTURE_FILE" | tee "$ROOT_DIR/artifacts/packet-analysis.txt"
if [[ ! -s "$ROOT_DIR/artifacts/packet-analysis.txt" ]]; then
    echo "No packets were decoded from the capture." >&2
    exit 1
fi
echo "Capture saved to artifacts/client-server.pcap"