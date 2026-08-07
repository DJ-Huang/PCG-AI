#!/usr/bin/env python3
"""Web pcg-editor import→export round-trip regression.

Bundles web/pcg-editor/scripts/roundtrip-check.ts with rolldown (from the
editor's own node_modules), runs it under node against golden .pcg fixtures,
and fails if any golden field is lost by the web import/export path.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EDITOR = ROOT / "web" / "pcg-editor"
ROLLDOWN = EDITOR / "node_modules" / ".bin" / "rolldown"
ENTRY = EDITOR / "scripts" / "roundtrip-check.ts"
FIXTURES = EDITOR / "scripts" / "fixtures"


def main() -> int:
    if not ROLLDOWN.exists():
        print(f"rolldown not found at {ROLLDOWN}; run `npm install` in web/pcg-editor.", file=sys.stderr)
        return 1

    fixtures = sorted(FIXTURES.glob("*.pcg"))
    if not fixtures:
        print(f"No .pcg fixtures under {FIXTURES}.", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="pcg-roundtrip-") as tmp:
        bundle = Path(tmp) / "roundtrip-check.mjs"
        build = subprocess.run(
            [str(ROLLDOWN), str(ENTRY), "--format", "esm", "--platform", "node", "--file", str(bundle)],
            capture_output=True,
            text=True,
        )
        if build.returncode != 0:
            print("rolldown bundle failed:", file=sys.stderr)
            print(build.stdout, file=sys.stderr)
            print(build.stderr, file=sys.stderr)
            return 1

        failures = 0
        for fixture in fixtures:
            run = subprocess.run(
                ["node", str(bundle), str(fixture)],
                capture_output=True,
                text=True,
            )
            sys.stdout.write(run.stdout)
            sys.stderr.write(run.stderr)
            failures += run.returncode != 0

    if failures:
        print(f"{failures} fixture(s) failed round-trip.", file=sys.stderr)
        return 1
    print(f"All {len(fixtures)} fixture(s) round-trip without field loss.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
