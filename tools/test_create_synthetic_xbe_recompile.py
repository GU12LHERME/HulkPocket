import pathlib
import struct
import tempfile
import tomllib
import unittest

from create_synthetic_xbe_recompile import BASE, ENTRY, THUNK, create

class SyntheticXbeRecompileTests(unittest.TestCase):
    def test_real_original_xbox_header_layout_and_code_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            xbe,config=create(pathlib.Path(directory))
            data=xbe.read_bytes()
            u32=lambda off: struct.unpack_from("<I",data,off)[0]
            self.assertEqual(data[:4],b"XBEH")
            self.assertEqual(u32(0x104),BASE)
            self.assertEqual(u32(0x128) ^ 0xA8FC57AB,ENTRY)
            self.assertEqual(u32(0x158) ^ 0x5B6D40B6,THUNK)
            self.assertEqual(u32(0x11C),1)
            self.assertEqual(u32(0x244),ENTRY)
            self.assertEqual(u32(0x24C),0x600)
            self.assertEqual(data[0x600:0x606],bytes.fromhex("B8 78 56 34 12 C3"))
            with config.open("rb") as stream:
                cfg=tomllib.load(stream)
            self.assertEqual(cfg["xbe"]["entry"],ENTRY)
            self.assertIn(".text",cfg["xbe"]["code_sections"])
            self.assertEqual(cfg["xbe"]["partitions"],1)

if __name__=="__main__":
    unittest.main()
