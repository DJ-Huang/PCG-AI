#!/usr/bin/env python3
"""CI wrapper: verify the builtin subgraph library index and synced copies."""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

if __name__ == "__main__":
    raise SystemExit(
        subprocess.call(
            [sys.executable, str(ROOT / "scripts" / "build_library_index.py"), "--check"],
            cwd=ROOT,
        )
    )
