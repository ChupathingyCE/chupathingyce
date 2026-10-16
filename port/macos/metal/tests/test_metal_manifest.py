"""Manifest coverage, upstream drift and compiled-symbol contract regressions."""
import copy
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from metal_paths import ROOT

# Load the standalone tool explicitly, also when the tests folder is the LSP root.
_spec = importlib.util.spec_from_file_location("metal_manifest_contract", ROOT / "tools/metal_manifest.py")
assert _spec is not None and _spec.loader is not None
contract = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = contract
_spec.loader.exec_module(contract)


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.manifest = contract.load()

    def test_tree_is_fully_classified(self):
        self.assertEqual(contract.structure_problems(self.manifest), [])

    def test_missing_source_is_rejected(self):
        manifest = copy.deepcopy(self.manifest)
        manifest.shared.pop()
        self.assertTrue(any("new upstream source" in p for p in contract.structure_problems(manifest)))

    def test_new_mac_file_is_rejected(self):
        manifest = copy.deepcopy(self.manifest)
        manifest.mac_headers.pop()
        self.assertTrue(any("not in manifest.json" in p for p in contract.structure_problems(manifest)))

    def test_duplicate_classification_is_rejected(self):
        manifest = copy.deepcopy(self.manifest)
        manifest.shared.append(next(iter(manifest.replaced)))
        self.assertTrue(any("pick one" in p for p in contract.structure_problems(manifest)))

    def test_acknowledgement_is_not_reconciliation(self):
        manifest = copy.deepcopy(self.manifest)
        entry = next(e for e in manifest.entries() if e.seen)
        with patch.object(contract, "upstream_blob", return_value=entry.seen):
            found = next(d for d in contract.drift(manifest) if d.entry is entry)
            self.assertTrue(found.known)
        with patch.object(contract, "upstream_blob", return_value="new-blob"):
            found = next(d for d in contract.drift(manifest) if d.entry is entry)
            self.assertFalse(found.known)

    def test_identical_drift_report(self):
        entry = next(e for e in self.manifest.entries() if e.identical)
        report = contract.Drift(entry, "changed", False).describe(self.manifest, None)
        self.assertIn("must stay identical", report)

    def test_preview_catches_new_sources(self):
        files = [*self.manifest.shared, *self.manifest.replaced, *self.manifest.dropped, "new_renderer.c"]
        with patch.object(contract, "git", return_value="\n".join(files)):
            problems = contract.reference_problems(self.manifest, "upstream/main")
        self.assertEqual(len(problems), 1)
        self.assertIn("new_renderer.c", problems[0])

    def test_preview_catches_removed_sources(self):
        files = [*self.manifest.shared, *self.manifest.replaced, *self.manifest.dropped]
        removed = files.pop()
        with patch.object(contract, "git", return_value="\n".join(files)):
            problems = contract.reference_problems(self.manifest, "upstream/main")
        self.assertEqual(len(problems), 1)
        self.assertIn(removed, problems[0])

    @unittest.skipUnless(shutil.which("clang") and shutil.which("nm"), "clang and nm required")
    def test_macro_exports_are_checked_from_objects(self):
        if sys.platform != "darwin":
            self.skipTest("Mach-O nm contract")
        with tempfile.TemporaryDirectory(prefix="metal contract ") as folder:
            root = Path(folder)
            objects = []
            for name, text in (("upstream", "#define EXPORT(n) void D3DDevice_##n(void) {}\nEXPORT(NewState)\n"),
                               ("metal", "void existing(void) {}\n")):
                source, obj = root / (name + ".c"), root / (name + ".o")
                source.write_text(text)
                subprocess.run(["clang", "-c", str(source), "-o", str(obj)], check=True)
                objects.append(obj)
            manifest = copy.deepcopy(self.manifest)
            manifest.not_provided = {}
            problems = contract.symbol_problems(manifest, objects[:1], objects[1:])
            self.assertTrue(any("D3DDevice_NewState" in p for p in problems))
            manifest.not_provided["D3DDevice_NewState"] = "test-only exception"
            self.assertEqual(contract.symbol_problems(manifest, objects[:1], objects[1:]), [])
            manifest.not_provided["obsolete"] = "no longer exported"
            self.assertTrue(any("remove it" in p for p in contract.symbol_problems(manifest, objects[:1], objects[1:])))
