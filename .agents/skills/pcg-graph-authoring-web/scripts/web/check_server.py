#!/usr/bin/env python3
"""Check that the PCG web editor (Vite dev server) and pcg-server are running.

Exit 0 = both healthy, exit 1 = one or both unreachable.
"""

import argparse
import json
import sys
import urllib.request

VITE_URL = "http://127.0.0.1:5173"
PCG_SERVER_URL = "http://127.0.0.1:17890/v1/health"


def check_url(url: str, timeout: float = 5.0) -> tuple[bool, str]:
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "pcg-check/1.0"})
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace").strip()
            return resp.status == 200, body
    except Exception as exc:
        return False, str(exc)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vite-url", default=VITE_URL, help="Vite dev server base URL")
    parser.add_argument("--pcg-server-url", default=PCG_SERVER_URL, help="pcg-server health endpoint")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    vite_ok, vite_body = check_url(f"{args.vite_url}/api/cook-health")
    pcg_ok, pcg_body = check_url(args.pcg_server_url)

    all_ok = vite_ok and pcg_ok

    if args.json:
        print(json.dumps({
            "healthy": all_ok,
            "vite": {"ok": vite_ok, "url": args.vite_url, "body": vite_body[:200]},
            "pcgServer": {"ok": pcg_ok, "url": args.pcg_server_url, "body": pcg_body[:200]},
        }, indent=2))
    else:
        print(f"Vite dev server: {'OK' if vite_ok else 'UNREACHABLE'} | {args.vite_url}")
        print(f"pcg-server: {'OK' if pcg_ok else 'UNREACHABLE'} | {args.pcg_server_url}")
        if not vite_ok:
            print("  → Start Vite: cd web/pcg-editor && npm run dev", file=sys.stderr)
        if not pcg_ok:
            print("  → Start pcg-server: ./scripts/run-pcg-server.sh", file=sys.stderr)

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
