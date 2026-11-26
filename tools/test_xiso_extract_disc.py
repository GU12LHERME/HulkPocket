import tempfile
import unittest
import struct
from pathlib import Path
from xiso_extract_disc import extract_disc

class DiscTest(unittest.TestCase):
    def test_nested_and_transaction(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);image=bytearray(40*2048)
            h=32*2048;image[h:h+20]=b'MICROSOFT*XBOX*MEDIA'
            struct.pack_into('<II',image,h+20,33,2048)
            def entry(sector,off,name,start,count,flags=0,right=0):
                at=sector*2048+off
                struct.pack_into('<HHIIBB',image,at,0,right,start,count,flags,len(name))
                image[at+14:at+14+len(name)]=name
            entry(33,0,b'default.xbe',35,4,right=8)
            entry(33,32,b'Data',34,2048,16)
            entry(34,0,b'a.bin',36,3)
            image[35*2048:35*2048+4]=b'XBEH'
            image[36*2048:36*2048+3]=b'abc'
            src=root/'test.iso';src.write_bytes(image)
            ref=root/'default.xbe';ref.write_bytes(b'XBEH')
            dest=root/'disc'
            extract_disc(src,dest,ref)
            self.assertEqual((dest/'Data'/'a.bin').read_bytes(),b'abc')
            self.assertEqual((dest/'default.xbe').read_bytes(),b'XBEH')
            self.assertEqual(src.read_bytes(),image)
            with self.assertRaises(FileExistsError):extract_disc(src,dest,ref)
            ref.write_bytes(b'bad')
            with self.assertRaises(ValueError):extract_disc(src,root/'wrong',ref)
            self.assertFalse((root/'wrong').exists())
            self.assertFalse(list(root.glob('.hulk-disc-*')))
            ref.write_bytes(b'XBEH')
            entry(34,0,b'../x',36,3)
            src.write_bytes(image)
            with self.assertRaises(ValueError):extract_disc(src,root/'unsafe',ref)
            self.assertFalse((root/'unsafe').exists())

if __name__=='__main__':unittest.main()
