"""Regression tests for the local RP6 executable identity check."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("check_game_version", TOOLS / "check_game_version.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class VersionIdentityTests(unittest.TestCase):
    def test_exact_rp6_executable_identity(self):
        with patch.object(mod, "inspect", return_value={
            "title_id": "56550039", "size": mod.REFERENCE_SIZE, "sha256": mod.REFERENCE_SHA256
        }):
            result = mod.verify(Path("fake.xbe"))
        self.assertTrue(result["matches_rp6_executable"])

    def test_rejects_same_title_id_but_different_executable(self):
        with patch.object(mod, "inspect", return_value={
            "title_id": "56550039", "size": mod.REFERENCE_SIZE, "sha256": "0" * 64
        }):
            result = mod.verify(Path("fake.xbe"))
        self.assertFalse(result["matches_rp6_executable"])
        self.assertTrue(result["checks"]["title_id"])
        self.assertFalse(result["checks"]["sha256"])

    def test_rejects_different_game(self):
        with patch.object(mod, "inspect", return_value={
            "title_id": "12345678", "size": mod.REFERENCE_SIZE, "sha256": mod.REFERENCE_SHA256
        }):
            result = mod.verify(Path("fake.xbe"))
        self.assertFalse(result["matches_rp6_executable"])


if __name__ == "__main__":
    unittest.main()
