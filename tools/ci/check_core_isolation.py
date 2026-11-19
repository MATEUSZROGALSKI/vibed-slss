#!/usr/bin/env python3
"""CI check: src/core must not include platform or toolkit headers (doc 5.1).

Exits non-zero and prints every offending line so PR authors see all
violations in one run instead of fixing them one at a time.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FORBIDDEN = re.compile(
    r"#\s*include\s*[<\"]("
    r"windows\.h|winsock.*\.h|shlobj\.h|dwmapi\.h|objbase\.h|"
    r"Qt[A-Za-z/]|Q[A-Z][A-Za-z]+\.h|q[a-z_]+\.h|"
    r"AppKit/|ScreenSaver/|Cocoa/|Foundation/|CoreGraphics/|"
    r"dbus/|wayland-|X11/"
    r")",
    re.IGNORECASE,
)

# #ifdef guards for OS APIs are equally forbidden in core (goal G6).
PLATFORM_IFDEF = re.compile(r"#\s*(if|ifdef|ifndef).*\b(_WIN32|__APPLE__|__linux__)\b")

SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".cc"}

# platform_iface is the single sanctioned seam between portable core and OS
# APIs — its per-OS .cpp files are selected by CMake, not #ifdefs, and its
# public header stays platform-free (checked below like any other file).
EXEMPT_DIRS = {"platform_iface"}


def scan(root: Path) -> int:
    violations = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        rel_parts = path.relative_to(root).parts
        if rel_parts and rel_parts[0] in EXEMPT_DIRS and "src" in rel_parts:
            continue  # only src/ impls are exempt; include/ headers still scanned
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for lineno, line in enumerate(lines, 1):
            if FORBIDDEN.search(line) or PLATFORM_IFDEF.search(line):
                violations.append(f"{path.relative_to(root)}:{lineno}: {line.strip()}")
    if violations:
        print("core isolation check FAILED — platform/toolkit references found:")
        print("\n".join(f"  {v}" for v in violations))
        return 1
    print(f"core isolation check OK ({root})")
    return 0


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    core = repo / "src" / "core"
    if not core.is_dir():
        print(f"error: {core} not found", file=sys.stderr)
        return 2
    return scan(core)


if __name__ == "__main__":
    sys.exit(main())
