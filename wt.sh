#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
REPO_NAME="$(basename "$REPO_ROOT")"

usage() {
    cat <<EOF
PCG-AI Worktree Manager

Usage:
  ./wt.sh new <name> [base-branch]   Create a worktree at ../{REPO_NAME}-{name}
                                     branch: wt/{name}, based on [base-branch] (default: current HEAD)
  ./wt.sh list                      List all worktrees
  ./wt.sh go <name>                 Print the path of a worktree (use: cd \$(./wt.sh go taskA))
  ./wt.sh rm <name>                 Remove a worktree and its branch
  ./wt.sh clean                     Remove all worktrees (keeps main repo)

Current worktrees:
EOF
    git -C "$REPO_ROOT" worktree list
}

cmd_new() {
    local name="$1"
    local base="${2:-}"
    local wt_dir="../${REPO_NAME}-${name}"
    local branch="wt/${name}"

    # Resolve base branch/commit
    if [[ -z "$base" ]]; then
        base="$(git -C "$REPO_ROOT" rev-parse HEAD)"
    fi

    # Safety: never use the main repo's checked-out branch
    local current_branch
    current_branch="$(git -C "$REPO_ROOT" branch --show-current)"
    if [[ "$branch" == "$current_branch" ]]; then
        echo "Error: branch '$branch' is the current branch of the main repo." >&2
        exit 1
    fi

    # Check if already exists
    if git -C "$REPO_ROOT" worktree list --porcelain | grep -q "^worktree ${wt_dir}$"; then
        echo "Error: worktree at ${wt_dir} already exists." >&2
        exit 1
    fi

    git -C "$REPO_ROOT" worktree add -b "$branch" "$wt_dir" "$base"
    echo ""
    echo "✅ Worktree created:"
    echo "   Path:   $(cd "$REPO_ROOT/$wt_dir" && pwd)"
    echo "   Branch: $branch"
    echo "   Base:   $base"
}

cmd_list() {
    git -C "$REPO_ROOT" worktree list
}

cmd_go() {
    local name="$1"
    local wt_dir="../${REPO_NAME}-${name}"
    local full_path
    full_path="$(cd "$REPO_ROOT/$wt_dir" 2>/dev/null && pwd)" || {
        echo "Error: worktree '$name' not found." >&2
        exit 1
    }
    echo "$full_path"
}

cmd_rm() {
    local name="$1"
    local wt_dir="../${REPO_NAME}-${name}"
    local branch="wt/${name}"

    git -C "$REPO_ROOT" worktree remove "$wt_dir" --force 2>/dev/null || true
    git -C "$REPO_ROOT" branch -D "$branch" 2>/dev/null || true
    echo "🗑️  Removed worktree: $name"
}

cmd_clean() {
    local main_worktree
    main_worktree="$(git -C "$REPO_ROOT" worktree list --porcelain | grep "^worktree " | head -1 | cut -d' ' -f2)"
    while IFS= read -r line; do
        local wt_path
        wt_path="$(echo "$line" | cut -d' ' -f2)"
        if [[ "$wt_path" != "$main_worktree" ]]; then
            git -C "$REPO_ROOT" worktree remove "$wt_path" --force 2>/dev/null || true
            echo "🗑️  Removed: $wt_path"
        fi
    done < <(git -C "$REPO_ROOT" worktree list --porcelain | grep "^worktree ")
    # Clean up wt/ branches
    git -C "$REPO_ROOT" branch | grep 'wt/' | sed 's/[* ]//g' | while read -r b; do
        git -C "$REPO_ROOT" branch -D "$b" 2>/dev/null || true
    done
    echo "✅ All worktrees removed (main repo untouched)."
}

# --- Main ---
case "${1:-}" in
    new)
        [[ -z "${2:-}" ]] && { echo "Error: missing <name>" >&2; usage; exit 1; }
        cmd_new "$2" "${3:-}"
        ;;
    list|"ls")
        cmd_list
        ;;
    go)
        [[ -z "${2:-}" ]] && { echo "Error: missing <name>" >&2; usage; exit 1; }
        cmd_go "$2"
        ;;
    rm)
        [[ -z "${2:-}" ]] && { echo "Error: missing <name>" >&2; usage; exit 1; }
        cmd_rm "$2"
        ;;
    clean)
        cmd_clean
        ;;
    *)
        usage
        exit 1
        ;;
esac
