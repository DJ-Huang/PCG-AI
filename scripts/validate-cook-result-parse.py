#!/usr/bin/env python3
"""Web pcg-editor cook-result binary parse contract.

Bundles web/pcg-editor/scripts/cook-result-check.ts with rolldown (from the
editor's own node_modules), runs it under node against golden cook-result
fixtures (pcg-server /v1/cook responses), and fails if the TS parser drifts
from the pcg-core/pcg-server binary contract.
"""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EDITOR = ROOT / "web" / "pcg-editor"
ROLLDOWN = EDITOR / "node_modules" / ".bin" / "rolldown"
ENTRY = EDITOR / "scripts" / "cook-result-check.ts"
FIXTURES = EDITOR / "scripts" / "fixtures"


def main() -> int:
    if not ROLLDOWN.exists():
        print(f"rolldown not found at {ROLLDOWN}; run `npm install` in web/pcg-editor.", file=sys.stderr)
        return 1

    pairs = sorted(
        (bin_path, bin_path.with_suffix(".json"))
        for bin_path in FIXTURES.glob("cook-result-*.bin")
    )
    pairs = [(b, m) for b, m in pairs if m.exists()]
    if not pairs:
        print(f"No cook-result fixtures (bin + json) under {FIXTURES}.", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="pcg-cook-result-") as tmp:
        bundle = Path(tmp) / "cook-result-check.mjs"
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
        for bin_path, meta_path in pairs:
            run = subprocess.run(
                ["node", str(bundle), str(bin_path), str(meta_path)],
                capture_output=True,
                text=True,
            )
            sys.stdout.write(run.stdout)
            sys.stderr.write(run.stderr)
            failures += run.returncode != 0

    if failures:
        print(f"{failures} fixture(s) failed cook-result parse.", file=sys.stderr)
        return 1
    print(f"All {len(pairs)} fixture(s) parse within contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
