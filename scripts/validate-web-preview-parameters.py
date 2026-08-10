#!/usr/bin/env python3
"""Bundle and execute the Web editor preview-parameter contract."""

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EDITOR = ROOT / "web" / "pcg-editor"
ROLLDOWN = EDITOR / "node_modules" / ".bin" / "rolldown"
ENTRY = EDITOR / "scripts" / "preview-parameters-check.ts"


def main() -> int:
    if not ROLLDOWN.exists():
        print(f"rolldown not found at {ROLLDOWN}; run `npm install` in web/pcg-editor.", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="pcg-web-preview-parameters-") as tmp:
        bundle = Path(tmp) / "preview-parameters-check.mjs"
        build = subprocess.run(
            [str(ROLLDOWN), str(ENTRY), "--format", "esm", "--platform", "node", "--file", str(bundle)],
            cwd=EDITOR,
            capture_output=True,
            text=True,
        )
        if build.returncode != 0:
            print("rolldown bundle failed:", file=sys.stderr)
            print(build.stdout, file=sys.stderr)
            print(build.stderr, file=sys.stderr)
            return build.returncode

        run = subprocess.run(["node", str(bundle)], cwd=EDITOR, capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        sys.stderr.write(run.stderr)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
