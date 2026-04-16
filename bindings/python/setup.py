from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py as _build_py


ROOT_DIR = Path(__file__).resolve().parents[2]
MODULE_DIR = Path(__file__).resolve().parent


def _native_lib_candidates(root: Path) -> list[Path]:
    return [root / "libxon.dylib", root / "libxon.so", root / "xon.dll"]


class build_py(_build_py):
    def run(self) -> None:
        self._build_native_library()
        super().run()
        self._copy_native_library_to_build()

    def _build_native_library(self) -> None:
        existing = [p for p in _native_lib_candidates(ROOT_DIR) if p.exists()]
        if existing:
            return

        if os.name == "nt":
            raise RuntimeError(
                "xon native library was not found. Build xon.dll first before creating the Python package."
            )

        build_script = ROOT_DIR / "build.sh"
        subprocess.check_call([str(build_script)], cwd=ROOT_DIR)

    def _copy_native_library_to_build(self) -> None:
        build_lib_dir = Path(self.build_lib)
        copied = False
        for candidate in _native_lib_candidates(ROOT_DIR):
            if candidate.exists():
                shutil.copy2(candidate, build_lib_dir / candidate.name)
                copied = True
        if not copied:
            raise RuntimeError("Failed to locate built native xon library for wheel packaging.")


setup(
    name="xerxisfy-xon",
    version="1.0.0",
    description="Python bindings for Xon (Xerxis Object Notation)",
    long_description=(ROOT_DIR / "README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    author="Isaac Muliro",
    author_email="opensource@xerxisfy.com",
    license="MIT",
    url="https://github.com/xerxisfy/xon",
    project_urls={
        "Source": "https://github.com/xerxisfy/xon",
        "Issues": "https://github.com/xerxisfy/xon/issues",
    },
    py_modules=["xon"],
    package_dir={"": "."},
    include_package_data=True,
    package_data={"": ["*.dylib", "*.so", "*.dll"]},
    python_requires=">=3.9",
    classifiers=[
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3 :: Only",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ],
    cmdclass={"build_py": build_py},
)
