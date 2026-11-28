import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import ndk_tools  # noqa: E402


class NdkToolsTest(unittest.TestCase):
    def test_bare_name_preferred(self):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = pathlib.Path(tmp)
            (bin_dir / "llvm-nm").write_text("")
            (bin_dir / "llvm-nm.exe").write_text("")
            self.assertEqual(ndk_tools.tool(bin_dir, "llvm-nm", windows=True), bin_dir / "llvm-nm")

    def test_windows_suffixes(self):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = pathlib.Path(tmp)
            (bin_dir / "llvm-readelf.exe").write_text("")
            (bin_dir / "aarch64-linux-android29-clang++.cmd").write_text("")
            self.assertEqual(ndk_tools.tool(bin_dir, "llvm-readelf", windows=True), bin_dir / "llvm-readelf.exe")
            self.assertEqual(ndk_tools.tool(bin_dir, "aarch64-linux-android29-clang++", windows=True),
                             bin_dir / "aarch64-linux-android29-clang++.cmd")
            # POSIX hosts never pick up Windows-suffixed files.
            self.assertEqual(ndk_tools.tool(bin_dir, "llvm-readelf", windows=False), bin_dir / "llvm-readelf")

    def test_missing_returns_bare_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            bin_dir = pathlib.Path(tmp)
            self.assertFalse(ndk_tools.tool(bin_dir, "llvm-nm", windows=True).is_file())

    def test_has_ndk_clang_per_host(self):
        for host, name in (("linux-x86_64", "clang"), ("windows-x86_64", "clang.exe"), ("darwin-arm64", "clang")):
            with tempfile.TemporaryDirectory() as tmp:
                ndk = pathlib.Path(tmp)
                self.assertFalse(ndk_tools.has_ndk_clang(ndk))
                bin_dir = ndk / "toolchains/llvm/prebuilt" / host / "bin"
                bin_dir.mkdir(parents=True)
                (bin_dir / name).write_text("")
                self.assertTrue(ndk_tools.has_ndk_clang(ndk), host)


class BuildXbeAndroidToolLookupTest(unittest.TestCase):
    def test_missing_ndk_tools_report_cleanly(self):
        # Regression: the tool lookup must not be shadowed inside build().
        import build_xbe_android
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            with self.assertRaises(FileNotFoundError):
                build_xbe_android.build(root, root, root / "bin", root / "out.so")


if __name__ == "__main__":
    unittest.main()
