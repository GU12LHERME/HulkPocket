"""Offline tests for Thunderclap native ARM64 build orchestration."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "build_native_engine", Path(__file__).resolve().parents[1] / "tools/build_native_engine.py"
)
# import sibling package wrapper just like script execution does
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class BuildNativeEngineTests(unittest.TestCase):
    def test_missing_inputs_fail_closed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            checks, _, cmds, pack, image = mod.make_plan(
                root, root, root / "bindings.json", root, root, root,
                root / "out", root / "out/hulk.apk", 2
            )
            self.assertFalse(all(checks.values()))
            self.assertEqual(cmds[0][2], "compile")
            self.assertEqual(cmds[1][2], "link")
            self.assertIn("--bindings", cmds[1])
            self.assertEqual(image, root / "out/link/libmain.so")
            self.assertIn("--execute", pack)

    def test_local_inputs_form_real_upstream_pipeline(self):
        with tempfile.TemporaryDirectory() as d:
            r = Path(d)
            upstream = r / "thunderclap"
            snap, ndk, sdl, ff = r / "snapshots", r / "ndk", r / "sdl-app", r / "ffmpeg"
            (snap / "snap").mkdir(parents=True)
            for dname in ("src/game", "include", "repo-include", "data/src"):
                (snap / "snap" / dname).mkdir(parents=True, exist_ok=True)
            (snap / "snap/src/game/test.cpp").write_text("int main_test = 0;")
            (snap / "snap/data/src/example.cpp").write_text("int example_data = 0;")
            manifest = '{"tus": [{"path": "src/game/test.cpp"}]}'
            (snap / "snap/manifest.json").write_text(manifest)
            for p in [
                upstream / "tools/native_elf.py",
                upstream / "tools/android_apk.py",
                upstream / "src/platform/sdl/crt/native_crt_table.h",
                ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin/clang",
                sdl / "lib/libSDL3.a",
                *[ff / "lib" / n for n in ("libavcodec.so", "libavformat.so", "libavutil.so", "libswresample.so")],
            ]:
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            bindings = r / "bindings.json"
            bindings.write_text('{"bindings":{"FUN_00010000":{"addr":"0x00010000","how":["reloc"]}}}')
            checks, env, cmds, _, _ = mod.make_plan(
                upstream, snap, bindings, ndk, sdl, ff, r / "output", r / "output/hulk.apk", 4
            )
            self.assertTrue(all(checks.values()))
            self.assertEqual(env["HULK_ANDROID_SDL"], str(sdl))
            self.assertEqual(env["HULK_CLANG_PROBE_DIR"], str(snap))
            self.assertEqual(cmds[0][-1], "4")
            (snap / "snap/manifest.json").write_text("{}")
            checks, _, _, _, _ = mod.make_plan(
                upstream, snap, bindings, ndk, sdl, ff, r / "output", r / "output/hulk.apk", 4
            )
            self.assertFalse(checks["generated_snapshot"])
            (snap / "snap/manifest.json").write_text(manifest)
            (snap / "snap/src/game/test.cpp").unlink()
            checks, _, _, _, _ = mod.make_plan(
                upstream, snap, bindings, ndk, sdl, ff, r / "output", r / "output/hulk.apk", 4
            )
            self.assertFalse(checks["generated_snapshot"])
            (snap / "snap/src/game/test.cpp").write_text("int main_test = 0;")
            bindings.write_text("{}")
            checks, _, _, _, _ = mod.make_plan(
                upstream, snap, bindings, ndk, sdl, ff, r / "output", r / "output/hulk.apk", 4
            )
            self.assertFalse(checks["generated_bindings"])
            bindings.write_text('{"bindings":{"FUN_00010000":{"addr":"0x00010000","how":["reloc"]}}}')
            checks, _, _, _, _ = mod.make_plan(
                upstream, snap, bindings, ndk, sdl, ff, r / "output", r / "output/hulk.apk", 4
            )
            self.assertTrue(checks["generated_bindings"])

if __name__ == "__main__":
    unittest.main()
