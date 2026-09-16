#!/usr/bin/env bash
# PICG entry point; retain the original launcher for existing integrations.
set -euo pipefail
exec "$(cd "$(dirname "$0")" && pwd)/run-pcg-web.sh" "$@"
