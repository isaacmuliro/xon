#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

echo "==> Running core tests"
./scripts/run_tests.sh

if command -v npm >/dev/null 2>&1; then
  echo "==> npm package preview"
  npm run -s pack:preview >/dev/null
  echo "npm pack dry-run: OK"
else
  echo "npm not found; skipping npm pack dry-run"
fi

if python3 -m build --help >/dev/null 2>&1; then
  echo "==> Building Python wheel"
  rm -rf dist-python
  python3 -m build --wheel bindings/python --outdir dist-python
  if python3 -c "import twine" >/dev/null 2>&1; then
    python3 -m twine check dist-python/*
  else
    echo "twine not installed; skipping twine check"
  fi
else
  echo "python build module not installed; skipping Python wheel check"
fi

if [ -d "vscode-xon" ] && [ -f "vscode-xon/package.json" ]; then
  echo "==> VS Code extension package check"
  (
    cd vscode-xon
    if command -v npx >/dev/null 2>&1 && [ -d node_modules ]; then
      NODE_MAJOR="$(node -p "process.versions.node.split('.')[0]")"
      if [ "${NODE_MAJOR}" -lt 20 ]; then
        echo "Node ${NODE_MAJOR} detected; skipping vsce package check (requires Node 20+)."
      elif npx @vscode/vsce package >/dev/null; then
        echo "vsce package: OK"
      else
        echo "vsce package failed; skipping (non-blocking for core package release)"
      fi
    else
      echo "npx or vscode-xon/node_modules missing; skipping vsce package check"
    fi
  )
fi

echo "Release checks completed."
