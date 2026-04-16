"""Python bindings for Xon using ctypes."""

import ctypes
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, Optional

XON_TYPE_NULL = 0
XON_TYPE_BOOL = 1
XON_TYPE_NUMBER = 2
XON_TYPE_STRING = 3
XON_TYPE_OBJECT = 4
XON_TYPE_LIST = 5


def _is_identifier(key: str) -> bool:
    if not key:
        return False
    if not (key[0].isalpha() or key[0] == "_"):
        return False
    return all(ch.isalnum() or ch == "_" for ch in key[1:])


def _to_xon(value: Any, indent: int = 2, depth: int = 0) -> str:
    pad = " " * (indent * depth)
    child_pad = " " * (indent * (depth + 1))

    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, list):
        if not value:
            return "[]"
        items = ",\n".join(f"{child_pad}{_to_xon(v, indent, depth + 1)}" for v in value)
        return "[\n" + items + f"\n{pad}]"
    if isinstance(value, dict):
        if not value:
            return "{}"
        parts = []
        for k, v in value.items():
            key = k if _is_identifier(k) else json.dumps(k)
            parts.append(f"{child_pad}{key}: {_to_xon(v, indent, depth + 1)}")
        return "{\n" + ",\n".join(parts) + f"\n{pad}}}"
    raise TypeError(f"Unsupported type for Xon serialization: {type(value).__name__}")


class XonParser:
    """ctypes wrapper around libxon."""

    def __init__(self, lib_path: Optional[str] = None) -> None:
        self._lib = ctypes.CDLL(str(self._resolve_library_path(lib_path)))
        self._setup_signatures()

    def _resolve_library_path(self, explicit: Optional[str]) -> Path:
        env_path = os.getenv("XON_LIB_PATH")
        if explicit:
            return Path(explicit).expanduser().resolve()
        if env_path:
            return Path(env_path).expanduser().resolve()

        module_dir = Path(__file__).resolve().parent
        module_candidates = [
            module_dir / "libxon.dylib",
            module_dir / "libxon.so",
            module_dir / "xon.dll",
        ]
        for candidate in module_candidates:
            if candidate.exists():
                return candidate

        root = Path(__file__).resolve().parents[2]
        if sys.platform == "darwin":
            candidate = root / "libxon.dylib"
        elif os.name == "nt":
            candidate = root / "xon.dll"
        else:
            candidate = root / "libxon.so"

        if not candidate.exists():
            raise FileNotFoundError(
                f"Could not find native library at {candidate}. Build it first with ./build.sh."
            )
        return candidate

    def _setup_signatures(self) -> None:
        self._lib.xonify.argtypes = [ctypes.c_char_p]
        self._lib.xonify.restype = ctypes.c_void_p

        self._lib.xonify_string.argtypes = [ctypes.c_char_p]
        self._lib.xonify_string.restype = ctypes.c_void_p

        self._lib.xon_free.argtypes = [ctypes.c_void_p]
        self._lib.xon_free.restype = None

        self._lib.xon_get_type.argtypes = [ctypes.c_void_p]
        self._lib.xon_get_type.restype = ctypes.c_int

        self._lib.xon_get_bool.argtypes = [ctypes.c_void_p]
        self._lib.xon_get_bool.restype = ctypes.c_int

        self._lib.xon_get_number.argtypes = [ctypes.c_void_p]
        self._lib.xon_get_number.restype = ctypes.c_double

        self._lib.xon_get_string.argtypes = [ctypes.c_void_p]
        self._lib.xon_get_string.restype = ctypes.c_char_p

        self._lib.xon_object_size.argtypes = [ctypes.c_void_p]
        self._lib.xon_object_size.restype = ctypes.c_size_t

        self._lib.xon_object_key_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self._lib.xon_object_key_at.restype = ctypes.c_char_p

        self._lib.xon_object_value_at.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self._lib.xon_object_value_at.restype = ctypes.c_void_p

        self._lib.xon_list_size.argtypes = [ctypes.c_void_p]
        self._lib.xon_list_size.restype = ctypes.c_size_t

        self._lib.xon_list_get.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self._lib.xon_list_get.restype = ctypes.c_void_p

    def _to_python(self, node_ptr: int) -> Any:
        if not node_ptr:
            return None

        value_type = self._lib.xon_get_type(node_ptr)

        if value_type == XON_TYPE_NULL:
            return None
        if value_type == XON_TYPE_BOOL:
            return bool(self._lib.xon_get_bool(node_ptr))
        if value_type == XON_TYPE_NUMBER:
            num = self._lib.xon_get_number(node_ptr)
            if float(num).is_integer():
                return int(num)
            return float(num)
        if value_type == XON_TYPE_STRING:
            raw = self._lib.xon_get_string(node_ptr)
            return raw.decode("utf-8") if raw else ""
        if value_type == XON_TYPE_OBJECT:
            size = self._lib.xon_object_size(node_ptr)
            out: Dict[str, Any] = {}
            for idx in range(size):
                raw_key = self._lib.xon_object_key_at(node_ptr, idx)
                value_ptr = self._lib.xon_object_value_at(node_ptr, idx)
                if raw_key:
                    out[raw_key.decode("utf-8")] = self._to_python(value_ptr)
            return out
        if value_type == XON_TYPE_LIST:
            size = self._lib.xon_list_size(node_ptr)
            return [self._to_python(self._lib.xon_list_get(node_ptr, idx)) for idx in range(size)]

        return None

    def parse_file(self, filename: str) -> Any:
        node = self._lib.xonify(filename.encode("utf-8"))
        if not node:
            raise ValueError(f"Failed to parse Xon file: {filename}")
        try:
            return self._to_python(node)
        finally:
            self._lib.xon_free(node)

    def parse_string(self, content: str) -> Any:
        node = self._lib.xonify_string(content.encode("utf-8"))
        if not node:
            raise ValueError("Failed to parse Xon string content")
        try:
            return self._to_python(node)
        finally:
            self._lib.xon_free(node)

    def dumps(self, obj: Any, indent: int = 2) -> str:
        return _to_xon(obj, indent=indent)


_default_parser = None


def _get_default_parser() -> XonParser:
    global _default_parser
    if _default_parser is None:
        _default_parser = XonParser()
    return _default_parser


def parse_file(filename: str) -> Any:
    return _get_default_parser().parse_file(filename)


def parse_string(content: str) -> Any:
    return _get_default_parser().parse_string(content)


def dumps(obj: Any, indent: int = 2) -> str:
    return _get_default_parser().dumps(obj, indent=indent)
