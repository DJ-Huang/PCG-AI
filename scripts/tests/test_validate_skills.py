"""Disposable fixtures for the skill linter; no editor/server/network access."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "validate-skills.py"
SPEC = importlib.util.spec_from_file_location("validate_skills", SCRIPT)
assert SPEC and SPEC.loader
LINT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LINT)


class SkillLintTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.skill = self.root / ".agents/skills/pcg-test/SKILL.md"
        self.write(self.skill, "---\nname: pcg-test\ndescription: Use to test a graph.\n---\n# Test\n")
        self.index = self.root / ".agents/skills/index.md"
        self.write(self.index, "[Test](pcg-test/SKILL.md)\n")

    @staticmethod
    def write(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def errors(self):
        return "\n".join(LINT.validate(self.root)["errors"])

    def append(self, text):
        self.write(self.skill, self.skill.read_text(encoding="utf-8") + text)

    def test_valid_fixture(self):
        self.assertEqual(self.errors(), "")

    def test_folded_description(self):
        self.write(self.skill, "---\nname: pcg-test\ndescription: >-\n  Use for graph\n  edits only.\n---\n")
        self.assertEqual(self.errors(), "")
        self.assertEqual(LINT.metadata(self.skill.read_text())[1], "Use for graph edits only.")

    def test_name_mismatch(self):
        self.write(self.skill, "---\nname: wrong\ndescription: Test.\n---\n")
        self.assertIn("metadata name", self.errors())

    def test_missing_description(self):
        self.write(self.skill, "---\nname: pcg-test\n---\n")
        self.assertIn("missing description", self.errors())

    def test_long_description(self):
        self.write(self.skill, "---\nname: pcg-test\ndescription: " + "x" * 301 + "\n---\n")
        self.assertIn("description exceeds", self.errors())

    def test_entry_budget(self):
        self.append("x" * 4096)
        self.assertIn("entry exceeds", self.errors())

    def test_missing_index_entry(self):
        self.write(self.index, "# No route\n")
        self.assertIn("not linked", self.errors())

    def test_missing_local_link(self):
        self.append("[Missing](missing.md)\n")
        self.assertIn("missing link target", self.errors())

    def test_existing_relative_link(self):
        self.write(self.root / ".agents/skills/shared/contract.md", "# Contract\n")
        self.append("[Contract](../shared/contract.md#section)\n")
        self.assertEqual(self.errors(), "")

    def test_external_and_fenced_links(self):
        self.append("[Docs](https://example.org/docs)\n```md\n[Example](missing.md)\n```\n")
        self.assertEqual(self.errors(), "")

    def test_reference_style_link(self):
        self.append("[Missing][ref]\n[ref]: missing.md\n")
        self.assertIn("missing link target", self.errors())

    def test_retired_reference(self):
        self.append("Use pcg-test-dev.\n")
        self.assertIn("retired PCG skill reference", self.errors())

    def test_retired_directory(self):
        (self.root / ".agents/skills/pcg-test-dev").mkdir()
        self.assertIn("retired PCG skill directory", self.errors())

    def test_home_path(self):
        self.append("Read `~/.cursor/skills/pcg-test/SKILL.md`.\n")
        self.assertIn("home skill path", self.errors())

    def test_windows_absolute_link(self):
        self.append("[Local](C:/work/skills/SKILL.md)\n")
        self.assertIn("nonportable link", self.errors())

    def test_escaping_link(self):
        self.append("[Outside](../../../../outside.md)\n")
        self.assertIn("escapes repository", self.errors())

    def test_empty_repository_fails(self):
        empty = self.root / "empty"
        empty.mkdir()
        self.assertTrue(LINT.validate(empty)["errors"])


if __name__ == "__main__":
    unittest.main()
