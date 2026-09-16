#!/usr/bin/env python3
"""Require repository Markdown documentation to be English.

README.ja.md is the only translated document. README.md may contain the
Japanese language-selector label, but no other CJK text.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXEMPT_FILES = {"README.ja.md"}
README_ALLOWED_TEXT = "日本語"


def repository_markdown() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def is_cjk(character: str) -> bool:
    codepoint = ord(character)
    return (
        0x3400 <= codepoint <= 0x4DBF
        or 0x4E00 <= codepoint <= 0x9FFF
        or 0xF900 <= codepoint <= 0xFAFF
        or 0x3040 <= codepoint <= 0x309F
        or 0x30A0 <= codepoint <= 0x30FF
    )


def main() -> int:
    failures: list[str] = []
    for relative_path in repository_markdown():
        if relative_path in EXEMPT_FILES:
            continue

        path = ROOT / relative_path
        if not path.is_file():
            continue
        for line_number, source_line in enumerate(
            path.read_text(encoding="utf-8").splitlines(), start=1
        ):
            line = source_line
            if relative_path == "README.md":
                line = line.replace(README_ALLOWED_TEXT, "")
            if any(is_cjk(character) for character in line):
                failures.append(f"{relative_path}:{line_number}")

    if failures:
        print("Non-English documentation found outside README.ja.md:")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print("Documentation language check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
