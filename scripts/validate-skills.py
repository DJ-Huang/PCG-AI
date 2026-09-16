#!/usr/bin/env python3
"""Lint PICG skill routing and portable Markdown references (stdlib only)."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit

MAX_DESCRIPTION_CHARS = 300
MAX_ENTRY_BYTES = 4096
RETIRED = re.compile(r"\bpcg-[a-z0-9-]*-dev\b")
HOME_SKILL = re.compile(r"~[/\\]\.[a-zA-Z0-9_-]+[/\\]skills(?:[/\\]|\b)")
LINK = re.compile(r"!?\[[^\]\n]*\]\(<?([^\s)>]+)>?(?:\s+\"[^\"]*\")?\)")
REFERENCE_LINK = re.compile(r"^\s*\[[^\]\n]+\]:\s*<?([^\s>]+)>?", re.M)


def metadata(text: str) -> tuple[str, str]:
    """Read flat name/description fields, including folded/literal descriptions.

    This is deliberately not a general YAML parser. Unsupported or missing
    metadata is rejected rather than interpreted as arbitrary YAML.
    """
    match = re.match(r"\A---\r?\n(.*?)\r?\n---(?:\r?\n|$)", text, re.S)
    if not match:
        return "", ""
    lines = match.group(1).splitlines()
    values: dict[str, str] = {}
    for index, line in enumerate(lines):
        key, sep, value = line.partition(":")
        if not sep or key not in {"name", "description"}:
            continue
        value = value.strip()
        if value in {">", ">-", "|", "|-"}:
            parts: list[str] = []
            for following in lines[index + 1:]:
                if following and not following[0].isspace():
                    break
                parts.append(following.strip())
            value = " ".join(parts).strip()
        elif len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values.get("name", ""), values.get("description", "")


def prose(text: str) -> str:
    """Remove fenced examples before checking Markdown link destinations."""
    lines: list[str] = []
    fence: tuple[str, int] | None = None
    for line in text.splitlines():
        mark = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if mark:
            token = mark.group(1)
            if fence is None:
                fence = (token[0], len(token))
            elif token[0] == fence[0] and len(token) >= fence[1]:
                fence = None
            continue
        if fence is None:
            lines.append(line)
    return "\n".join(lines)


def local_links(path: Path, text: str, root: Path) -> tuple[set[Path], list[str]]:
    targets: set[Path] = set()
    errors: list[str] = []
    clean = prose(text)
    for destination in LINK.findall(clean) + REFERENCE_LINK.findall(clean):
        if re.match(r"^[A-Za-z]:[/\\]", destination):
            errors.append(f"nonportable link: {destination}")
            continue
        parsed = urlsplit(destination)
        if parsed.scheme and parsed.scheme != "file":
            continue
        if not parsed.path:  # An anchor within this document.
            continue
        value = unquote(parsed.path)
        if parsed.scheme == "file" or value.startswith(("/", "~", "\\")):
            errors.append(f"nonportable link: {destination}")
            continue
        target = (path.parent / value).resolve()
        if not target.is_relative_to(root):
            errors.append(f"link escapes repository: {destination}")
        elif not target.exists():
            errors.append(f"missing link target: {destination}")
        else:
            targets.add(target)
    return targets, errors


def validate(root: Path) -> dict[str, object]:
    root = root.resolve()
    skills_root = root / ".agents/skills"
    entries = sorted(skills_root.glob("*/SKILL.md"))
    errors: list[str] = []
    if not entries:
        errors.append("no skill entries found under .agents/skills")
    documents = set(skills_root.rglob("*.md"))
    for relative in ("AGENTS.md", ".picg/rules/graph-authoring/graph-contract.md",
                     ".picg/rules/graph-authoring/triview.md"):
        path = root / relative
        if path.exists():
            documents.add(path)
    index = skills_root / "index.md"
    if not index.exists():
        errors.append("missing .agents/skills/index.md")
    documents.add(index)
    contents: dict[Path, str] = {}
    links: dict[Path, set[Path]] = {}
    for path in sorted(documents):
        if not path.exists():
            continue
        label = path.relative_to(root).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append(f"{label}: cannot read UTF-8: {exc}")
            continue
        contents[path] = text
        if RETIRED.search(text):
            errors.append(f"{label}: retired PCG skill reference")
        if HOME_SKILL.search(text):
            errors.append(f"{label}: editor-specific home skill path")
        targets, link_errors = local_links(path, text, root)
        links[path] = targets
        errors.extend(f"{label}: {message}" for message in link_errors)
    for directory in skills_root.iterdir() if skills_root.is_dir() else ():
        if directory.is_dir() and directory.name.startswith("pcg-") and "-dev" in directory.name:
            errors.append(f"retired PCG skill directory: {directory.name}")
    descriptions: dict[str, int] = {}
    entry_bytes: dict[str, int] = {}
    for entry in entries:
        name, description = metadata(contents.get(entry, ""))
        directory_name = entry.parent.name
        if name != directory_name:
            errors.append(f"{directory_name}: metadata name must match directory")
        if not description:
            errors.append(f"{directory_name}: missing description")
        elif len(description) > MAX_DESCRIPTION_CHARS:
            errors.append(f"{directory_name}: description exceeds {MAX_DESCRIPTION_CHARS} characters")
        descriptions[directory_name] = len(description)
        size = len(contents.get(entry, "").encode("utf-8"))
        entry_bytes[directory_name] = size
        if size > MAX_ENTRY_BYTES:
            errors.append(f"{directory_name}: entry exceeds {MAX_ENTRY_BYTES} bytes; move detail to references")
        if entry.resolve() not in links.get(index, set()):
            errors.append(f"{directory_name}: not linked from skills index")
    return {"skills": len(entries), "documents": len(contents),
            "entry_bytes": entry_bytes, "description_chars": descriptions,
            "errors": errors}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = validate(args.repo)
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Skills: {report['skills']} | documents: {report['documents']}")
        for error in report["errors"]:
            print(f"ERROR: {error}")
        print("FAIL" if report["errors"] else "PASS")
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
