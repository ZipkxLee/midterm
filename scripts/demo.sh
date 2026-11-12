#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
echo "Run the complete live demonstration and save its outputs with: make test"
echo "Then inspect artifacts/robustness-examples.txt and artifacts/concurrency-proof.txt."
echo
echo "Interactive example:"
echo "  ./build/server --verbose 127.0.0.1 5050"
echo "  ./build/client --verbose 127.0.0.1 5050 INFO"
echo "  ./build/client 127.0.0.1 5050 PING"
echo "  ./build/client 127.0.0.1 5050 'HOLD 3'"