import tempfile
import unittest
import struct
from pathlib import Path
from xiso_extract import extract

class ExtractionTest(unittest.TestCase):
    def test_extract(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            image = bytearray(36 * 2048)
            h = 32 * 2048
            image[h:h+20] = b'MICROSOFT*XBOX*MEDIA'
            struct.pack_into('<II', image, h+20, 33, 2048)
            d = 33 * 2048
            struct.pack_into('<HHIIBB', image, d, 0, 0, 34, 1024, 0, 11)
            image[d+14:d+25] = b'default.xbe'
            x = 34 * 2048
            image[x:x+4] = b'XBEH'
            struct.pack_into('<I', image, x+0x104, 0x10000)
            struct.pack_into('<I', image, x+0x118, 0x10200)
            struct.pack_into('<I', image, x+0x208, 123)
            struct.pack_into('<I', image, x+0x2a0, 1)
            src = root/'test.iso'
            src.write_bytes(image)
            out = root/'default.xbe'
            self.assertEqual(extract(src, out)['title_id'], '0000007B')
            self.assertEqual(src.read_bytes(), image)
            with self.assertRaises(FileExistsError):
                extract(src, out)

    def test_invalid(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp)/'bad.iso'
            src.write_bytes(b'bad')
            with self.assertRaises(ValueError):
                extract(src, Path(tmp)/'default.xbe')

if __name__ == '__main__':
    unittest.main()
