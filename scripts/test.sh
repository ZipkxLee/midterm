#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
mkdir -p artifacts
ROBUST_PORT="${ROBUST_PORT:-25050}"
LEGACY_PORT="${LEGACY_PORT:-25051}"
CLIENT_COUNT="${CLIENT_COUNT:-100}"
SERVER_LOG="$ROOT_DIR/artifacts/server-test.log"
LEGACY_LOG="$ROOT_DIR/artifacts/server-legacy.log"
EXAMPLE_LOG="$ROOT_DIR/artifacts/robustness-examples.txt"
PROOF_LOG="$ROOT_DIR/artifacts/concurrency-proof.txt"

./build/server --verbose 127.0.0.1 "$ROBUST_PORT" >"$SERVER_LOG" 2>&1 &
ROBUST_PID=$!
./build/server --legacy-demo 127.0.0.1 "$LEGACY_PORT" >"$LEGACY_LOG" 2>&1 &
LEGACY_PID=$!
cleanup() {
    kill "$ROBUST_PID" "$LEGACY_PID" 2>/dev/null || true
    wait "$ROBUST_PID" "$LEGACY_PID" 2>/dev/null || true
}
trap cleanup EXIT

ready=0
for attempt in $(seq 1 200); do
    if ./build/client 127.0.0.1 "$ROBUST_PORT" PING >/dev/null 2>&1 &&
       ./build/client 127.0.0.1 "$LEGACY_PORT" PING >/dev/null 2>&1; then
        ready=1
        break
    fi
done
if [[ "$ready" != 1 ]]; then
    echo "Servers did not become ready; inspect artifacts/server-test.log" >&2
    exit 1
fi

{
    echo "=== Basic client/server checks ==="
    ./build/client 127.0.0.1 "$ROBUST_PORT" PING
    ./build/client 127.0.0.1 "$ROBUST_PORT" INFO
    echo
    echo "=== Three robustness comparisons ==="
    python3 - "$ROBUST_PORT" "$LEGACY_PORT" <<'PY'
import socket
import sys

robust_port, legacy_port = map(int, sys.argv[1:])
examples = [
    ("1. Missing newline", b"PING"),
    ("2. Overlong frame", b"PING" + b"x" * 300 + b"\n"),
    ("3. Trailing garbage", b"INFO garbage\n"),
]

def request(port, payload):
    with socket.create_connection(("127.0.0.1", port), timeout=8) as connection:
        connection.settimeout(8)
        connection.sendall(payload)
        response = bytearray()
        while b"\n" not in response:
            chunk = connection.recv(4096)
            if not chunk:
                break
            response.extend(chunk)
        return response.decode(errors="replace").strip()

for title, payload in examples:
    robust = request(robust_port, payload)
    legacy = request(legacy_port, payload)
    print(title)
    print(f"  robust: {robust}")
    print(f"  legacy: {legacy}")
    if title == "1. Missing newline":
        assert "timed out" in robust and legacy == "OK PONG", (robust, legacy)
    elif title == "2. Overlong frame":
        assert "too long" in robust and legacy == "OK PONG", (robust, legacy)
    else:
        assert "invalid request" in robust and legacy.startswith("OK host="), (robust, legacy)

boundary_ok = request(robust_port, b"x" * 255 + b"\n")
boundary_long = request(robust_port, b"x" * 256 + b"\n")
print("Frame boundary")
print(f"  255-byte request: {boundary_ok}")
print(f"  256-byte request: {boundary_long}")
assert "too long" not in boundary_ok and "too long" in boundary_long

with socket.create_connection(("127.0.0.1", robust_port), timeout=8) as connection:
    connection.settimeout(8)
    connection.sendall(b"PING")
    connection.shutdown(socket.SHUT_WR)
    incomplete = connection.recv(4096).decode(errors="replace").strip()
    print(f"  EOF before newline: {incomplete}")
    assert "incomplete request" in incomplete
PY
} | tee "$EXAMPLE_LOG"

if ! [[ "$CLIENT_COUNT" =~ ^[1-9][0-9]*$ ]]; then
    echo "CLIENT_COUNT must be a positive integer." >&2
    exit 1
fi

echo "=== $CLIENT_COUNT concurrent clients (each holds one child for 3 seconds) ===" | tee "$PROOF_LOG"
client_pids=()
start_ns="$(date +%s%N)"
for client_number in $(seq 1 "$CLIENT_COUNT"); do
    ./build/client 127.0.0.1 "$ROBUST_PORT" "HOLD 3" \
        >"artifacts/concurrent-client-${client_number}.txt" 2>&1 &
    client_pids+=("$!")
done
for client_index in "${!client_pids[@]}"; do
    if wait "${client_pids[$client_index]}"; then
        :
    fi
done
end_ns="$(date +%s%N)"
elapsed_ns=$((end_ns - start_ns))
elapsed_seconds="$(awk -v ns="$elapsed_ns" 'BEGIN { printf "%.2f", ns / 1000000000 }')"
response_count=0
for client_number in $(seq 1 "$CLIENT_COUNT"); do
    if grep -q '^OK held 3 seconds$' "artifacts/concurrent-client-${client_number}.txt"; then
        response_count=$((response_count + 1))
    fi
done
fork_count="$(grep -c 'forked worker pid=' "$SERVER_LOG" || true)"
printf 'Clients completed successfully: %s/%s\nElapsed wall time: %s seconds\nServer fork log entries: %s\n' \
    "$response_count" "$CLIENT_COUNT" "$elapsed_seconds" "$fork_count" | tee -a "$PROOF_LOG"
grep 'forked worker pid=' "$SERVER_LOG" | tail -n "$CLIENT_COUNT" | tee -a "$PROOF_LOG"
if [[ "$response_count" != "$CLIENT_COUNT" ]] || (( elapsed_ns >= 15000000000 )) || (( fork_count < CLIENT_COUNT )); then
    echo "Concurrency proof failed: expected $CLIENT_COUNT workers finishing in under 15 seconds." | tee -a "$PROOF_LOG"
    exit 1
fi
sequential_seconds=$((CLIENT_COUNT * 3))
echo "PASS: $CLIENT_COUNT clients overlapped; sequential handling would take about $sequential_seconds seconds." | tee -a "$PROOF_LOG"
echo "Test evidence saved under artifacts/."