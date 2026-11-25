"""Offline tests for the native engine APK packaging gate."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "package_engine_apk", Path(__file__).resolve().parents[1] / "tools/package_engine_apk.py"
)
mod = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(mod)


def arm64_elf(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    h = bytearray(20)
    h[:4] = b"\x7fELF"
    h[4] = 2
    h[5] = 1
    h[18:20] = (183).to_bytes(2, "little")
    path.write_bytes(h)


class PackageEngineTests(unittest.TestCase):
    def test_reject_missing_and_x86(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp) / "libmain.so"
            self.assertFalse(mod.is_elf64_aarch64(p))
            p.write_bytes(b"not an ELF")
            self.assertFalse(mod.is_elf64_aarch64(p))
            arm64_elf(p)
            self.assertTrue(mod.is_elf64_aarch64(p))
            b = bytearray(p.read_bytes())
            b[18:20] = (62).to_bytes(2, "little")
            p.write_bytes(b)
            self.assertFalse(mod.is_elf64_aarch64(p))

    def test_complete_local_package_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            upstream, ffmpeg, engine = root / "upstream", root / "ffmpeg", root / "libmain.so"
            for name in [
                "tools/android_apk.py",
                "src/platform/sdl/android/java/org/thunderclap/hulkud/HulkActivity.java",
                "build/third_party/src/SDL/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java",
            ]:
                p = upstream / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            arm64_elf(engine)
            for name in ["libavcodec.so", "libavformat.so", "libavutil.so", "libswresample.so"]:
                arm64_elf(ffmpeg / "lib" / name)
            self.assertTrue(all(mod.requirements(upstream, engine, ffmpeg).values()))
            (ffmpeg / "lib/libavcodec.so").unlink()
            self.assertFalse(mod.requirements(upstream, engine, ffmpeg)["ffmpeg_avcodec_arm64"])


if __name__ == "__main__":
    unittest.main()
