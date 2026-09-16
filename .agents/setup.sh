#!/usr/bin/env bash
# PCG-AI skills setup — junction .agents/skills to user profile
set -euo pipefail

SKILLS_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SKILLS_DIR/.." && pwd)"
MODE="${1:-sync}"

echo
echo "============================================================"
echo "  PCG-AI Skills Setup  [$MODE]"
echo "  Skills: $SKILLS_DIR"
echo "  Repo:  $REPO_ROOT"
echo "============================================================"
echo

command -v node >/dev/null || { echo "[ERROR] Node.js required: https://nodejs.org/"; exit 1; }

if [[ "$MODE" == "unlink" ]]; then
  node "$REPO_ROOT/scripts/setup-agent-skills.mjs" unlink --once
else
  node "$REPO_ROOT/scripts/setup-agent-skills.mjs" --once
fi

echo
read -r -p "Press Enter to close..."
