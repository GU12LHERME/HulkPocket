"""Synthetic XBE header tests: no copyrighted game bytes are needed."""
import struct
import tempfile
from pathlib import Path
import unittest

from xbe_info import inspect, ENTRY_KEYS


def mock_xbe(title_id=0x56550039, entry=0x0020AAC6, flavor="retail"):
    data = bytearray(0x400)
    data[:4] = b"XBEH"
    base = 0x10000
    struct.pack_into("<I", data, 0x104, base)
    struct.pack_into("<I", data, 0x10C, 0x600000)
    struct.pack_into("<I", data, 0x118, base + 0x200)
    struct.pack_into("<I", data, 0x128, entry ^ ENTRY_KEYS[flavor])
    struct.pack_into("<I", data, 0x208, title_id)
    title = "Hulk".encode("utf-16le")
    data[0x20C:0x20C + len(title)] = title
    struct.pack_into("<I", data, 0x2A0, 4)
    return data


class XbeEntryTests(unittest.TestCase):
    def inspect_bytes(self, data):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "default.xbe"
            p.write_bytes(data)
            return inspect(p)

    def test_eu_retail(self):
        result = self.inspect_bytes(mock_xbe())
        self.assertEqual(result["hulk_recompiled_candidate"], "eu")
        self.assertEqual(result["entry_candidates"]["retail"], "0x0020AAC6")

    def test_usa_retail(self):
        result = self.inspect_bytes(mock_xbe(entry=0x0020A016))
        self.assertEqual(result["hulk_recompiled_candidate"], "usa")

    def test_debug_encoded_xbe(self):
        result = self.inspect_bytes(mock_xbe(flavor="debug"))
        self.assertEqual(result["entry_candidates"]["debug"], "0x0020AAC6")

    def test_wrong_title_never_claims_compatibility(self):
        result = self.inspect_bytes(mock_xbe(title_id=0xDEADBEEF))
        self.assertIsNone(result["hulk_recompiled_candidate"])

    def test_invalid_executable_does_not_claim_region(self):
        result = self.inspect_bytes(mock_xbe(entry=0x0060AAC6))
        self.assertIsNone(result["hulk_recompiled_candidate"])

    def test_rejects_truncated_xbe(self):
        with self.assertRaises(ValueError):
            self.inspect_bytes(b"XBEH" + bytes(0x110))


if __name__ == "__main__":
    unittest.main()
