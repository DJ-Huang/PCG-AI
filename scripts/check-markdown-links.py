#!/usr/bin/env python3
"""Check that repository-local Markdown links resolve to existing paths."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
INLINE_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")


def markdown_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "*.md"],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    return [ROOT / name for name in result.stdout.splitlines() if name and (ROOT / name).is_file()]


def local_target(raw_target: str) -> str | None:
    target = raw_target.strip()
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1]
    if " " in target and not target.startswith(("/", "./", "../")):
        target = target.split(maxsplit=1)[0]
    if not target or target.startswith(("#", "http://", "https://", "mailto:")):
        return None
    return unquote(target.split("#", 1)[0].split("?", 1)[0])


def main() -> int:
    failures: list[str] = []
    for markdown in markdown_files():
        for line_number, line in enumerate(markdown.read_text(encoding="utf-8").splitlines(), 1):
            for match in INLINE_LINK.finditer(line):
                target = local_target(match.group(1))
                if target is None:
                    continue
                resolved = (markdown.parent / target).resolve()
                if not resolved.exists():
                    failures.append(
                        f"{markdown.relative_to(ROOT)}:{line_number}: {match.group(1)}"
                    )

    if failures:
        print("Broken repository-local Markdown links:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("Markdown link check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

