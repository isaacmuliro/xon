#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

node "$ROOT_DIR/bin/xon" validate "$ROOT_DIR/tests/test.xon"

rm -rf /tmp/xon-build-smoke
node "$ROOT_DIR/bin/xon" build "$ROOT_DIR/examples" /tmp/xon-build-smoke
test -f /tmp/xon-build-smoke/config.json

TMP_DIR="$(mktemp -d /tmp/xon-config-smoke.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

mkdir -p "$TMP_DIR/config"
cp "$ROOT_DIR/examples/config.xon" "$TMP_DIR/config/app.xon"
cat > "$TMP_DIR/xon.config.json" <<'JSON'
{
  "build": {
    "input": "config/app.xon",
    "output": "src/generated/app-config.json"
  }
}
JSON

(
  cd "$TMP_DIR"
  node "$ROOT_DIR/bin/xon" build
  test -f src/generated/app-config.json
)
