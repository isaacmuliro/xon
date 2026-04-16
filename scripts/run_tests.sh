#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

"$ROOT_DIR/build.sh" >/dev/null

gcc -Wall -Wextra -std=c99 -I"$ROOT_DIR/include" \
    -o /tmp/xon_test_suite \
    "$ROOT_DIR/tests/test_suite.c" "$ROOT_DIR/src/xon_api.c" "$ROOT_DIR/src/lexer.c" "$ROOT_DIR/src/logger.c"
/tmp/xon_test_suite

python3 "$ROOT_DIR/tests/test_python.py"
