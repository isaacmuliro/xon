#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from bindings.python.xon import XonParser


def main() -> None:
    root = ROOT
    parser = XonParser(str(root / "libxon.dylib" if (root / "libxon.dylib").exists() else root / "libxon.so"))

    parsed = parser.parse_file(str(root / "examples" / "config.xon"))
    assert parsed["app_name"].startswith("XonApp")
    assert parsed["server"]["port"] == 8080
    assert parsed["database"]["pool_size"] == 0x14

    parsed_inline = parser.parse_string('{ name: "PyTest", count: 0x2A, ok: true }')
    assert parsed_inline == {"name": "PyTest", "count": 42, "ok": True}

    dumped = parser.dumps({"app_name": "python", "features": ["a", "b"], "enabled": True}, indent=2)
    reparsed = parser.parse_string(dumped)
    assert reparsed["app_name"] == "python"
    assert reparsed["features"] == ["a", "b"]
    assert reparsed["enabled"] is True

    print(json.dumps({"status": "ok", "python_binding": "passed"}))


if __name__ == "__main__":
    main()
