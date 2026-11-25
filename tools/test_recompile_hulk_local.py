import pathlib
import struct
import tempfile
import unittest
from unittest.mock import patch
from recompile_hulk_local import (
    TITLE_ID, ENTRIES, config_for_region, inspect_xbe, run,
)

def synthetic_xbe(entry=ENTRIES["eu"], title=TITLE_ID, debug=False):
    body=bytearray(0x400)
    body[:4]=b"XBEH"
    def set32(where,value):
        struct.pack_into("<I",body,where,value)
    base=0x10000
    set32(0x104,base)
    set32(0x10C,0x400000)
    set32(0x118,base+0x200)
    set32(0x208,title)
    set32(0x128,entry ^ (0x94859D4B if debug else 0xA8FC57AB))
    set32(0x158,0x210000 ^ (0xEFB1F152 if debug else 0x5B6D40B6))
    return body

class LocalRecompilerTests(unittest.TestCase):
    def test_region(self):
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/"default.xbe"
            for region,address in ENTRIES.items():
                f.write_bytes(synthetic_xbe(address))
                self.assertEqual(inspect_xbe(f),(TITLE_ID,address,region))
            f.write_bytes(synthetic_xbe(ENTRIES["eu"],debug=True))
            self.assertEqual(inspect_xbe(f)[2],"eu")
    def test_reject_foreign_or_unknown_image(self):
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/"default.xbe"
            f.write_bytes(synthetic_xbe(title=0x1234))
            with self.assertRaises(ValueError): inspect_xbe(f)
            f.write_bytes(synthetic_xbe(entry=0x123456))
            with self.assertRaises(ValueError): inspect_xbe(f)
            f.write_bytes(b"XBEH")
            with self.assertRaises(ValueError): inspect_xbe(f)
    def test_config_pin(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            (root/"config").mkdir()
            (root/"config"/"game.toml").write_text(
                '[xbe]\ntitle=0x56550039\nentry=0x20AAC6\nxdk=5849\n'
            )
            self.assertEqual(config_for_region(root,"eu").name,"game.toml")
            (root/"config"/"game.toml").write_text(
                '[xbe]\ntitle=0x56550039\nentry=0x20A016\nxdk=5849\n'
            )
            with self.assertRaises(ValueError): config_for_region(root,"eu")
    def test_local_driver_uses_private_paths(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            (root/"config").mkdir()
            (root/"config"/"game.toml").write_text(
                '[xbe]\ntitle=0x56550039\nentry=0x20AAC6\nxdk=5849\n'
            )
            sdk=root/"sdk"
            (sdk/"xdk"/"5849").mkdir(parents=True)
            (sdk/"xdk"/"5849"/"layout.toml").write_text("")
            xbe=root/"default.xbe"
            xbe.write_bytes(synthetic_xbe())
            compiler=root/"xbe_recomp_host"
            compiler.write_text("")
            out=root/"private_generated"
            def compile_fake(cmd,check):
                self.assertEqual(cmd[0],str(compiler))
                self.assertEqual(cmd[1],str(root/"config"/"game.toml"))
                self.assertEqual(cmd[2],str(xbe))
                self.assertTrue(check)
                self.assertEqual(cmd[-2:],[ "--xdk",str(sdk/"xdk") ])
                out.mkdir(exist_ok=True)
                (out/"x86_func_mapping.cpp").write_text("mapping")
                (out/"x86_recomp.0.cpp").write_text("function")
            with patch("recompile_hulk_local.subprocess.run",side_effect=compile_fake):
                self.assertEqual(run(compiler,sdk,root,xbe,out),out)

if __name__=="__main__":
    unittest.main()
