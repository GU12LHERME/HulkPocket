"""Offline tests for safe local disc preparation; no copyrighted input."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from subprocess import CompletedProcess

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_disc import extract


class PrepareDiscTests(unittest.TestCase):
    def test_refuses_to_overwrite_output(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            iso, out = root / "game.iso", root / "extracted"
            iso.write_bytes(b"not a real game")
            out.mkdir()
            with self.assertRaisesRegex(ValueError, "already exists"):
                extract(iso, out)

    def test_validates_xbe_before_commit(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            iso, out = root / "game.iso", root / "extracted"
            iso.write_bytes(b"test fixture")
            def mock_unpack(cmd, check):
                Path(cmd[3], "default.xbe").write_bytes(b"synthetic")
                return CompletedProcess(cmd, 0)
            fake_report = {
                "matches_rp6_executable": True,
                "reference": {"title_id": "56550039", "sha256": "hash"}
            }
            with patch("prepare_disc.shutil.which", return_value="/usr/bin/xdvdfs"), \
                 patch("prepare_disc.subprocess.run", side_effect=mock_unpack), \
                 patch("prepare_disc.verify_game_version", return_value=fake_report):
                report = extract(iso, out)
            self.assertTrue(report["verified"])
            self.assertTrue((out / "default.xbe").exists())
            self.assertFalse(list(root.glob(".hulkpocket-extract-*")))

    def test_version_mismatch_rolls_back(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            iso, out = root / "game.iso", root / "extracted"
            iso.write_bytes(b"test fixture")
            def mock_unpack(cmd, check):
                Path(cmd[3], "default.xbe").write_bytes(b"synthetic")
                return CompletedProcess(cmd, 0)
            with patch("prepare_disc.shutil.which", return_value="/usr/bin/xdvdfs"), \
                 patch("prepare_disc.subprocess.run", side_effect=mock_unpack), \
                 patch("prepare_disc.verify_game_version", return_value={"matches_rp6_executable": False}):
                with self.assertRaisesRegex(RuntimeError, "does not match"):
                    extract(iso, out)
            self.assertFalse(out.exists())
            self.assertFalse(list(root.glob(".hulkpocket-extract-*")))


if __name__ == "__main__":
    unittest.main()
