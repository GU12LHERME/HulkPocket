import importlib.util
import pathlib
import tempfile
import unittest

MODULE = pathlib.Path(__file__).with_name("prepare_xbe_elf.py")
spec = importlib.util.spec_from_file_location("prepare_xbe_elf", MODULE)
assert spec and spec.loader
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class PrepareXbeElfTests(unittest.TestCase):
    def test_export_rewrite_keeps_game_code(self):
        source = ('#include "x86_recomp_shared.h"\n'
                  'const uint32_t X86CodeBase=0x300000;\n'
                  'const uint32_t X86CodeEnd=0x310000;\n'
                  'const uint32_t X86ImageBase=0x10000;\n'
                  'const uint32_t X86ImageEnd=0x400000;\n'
                  'const uint32_t X86EntryPoint=0x300010;\n'
                  'const uint32_t X86KernelThunk=0xF000;\n'
                  'const X86FuncMapping X86FuncMappings[] = {{0x300010,sub_00300010},{0,nullptr}};\n'
                  'const uint32_t X86Continuations[]={0};\n')
        output=mod.rewrite_mapping(source)
        self.assertEqual(output.count('extern "C"'), 8)
        self.assertIn('extern "C" __attribute__((visibility("default"))) const X86FuncMapping X86FuncMappings[]',output)
        self.assertIn('sub_00300010',output)
    def test_sdk_linkage_block(self):
        input_header = ('abc\n#ifdef X86_GAME_CODE\n'
                        '#define X86_HOST_API __declspec(dllimport)\n'
                        '#else\n#define X86_HOST_API __declspec(dllexport)\n'
                        '#endif\n\n#define X86_FUNC(name) foo\n')
        out=mod.rewrite_sdk_ops(input_header)
        self.assertIn('visibility("default")',out)
        self.assertIn('weak, alias',out)
        self.assertNotIn('__declspec',out)
        self.assertIn('#define X86_FUNC(name) foo',out)
    def test_fail_closed_on_generator_drift(self):
        with self.assertRaises(ValueError):
            mod.rewrite_mapping('const X86FuncMapping X86FuncMappings[] = {};')
        with self.assertRaises(ValueError):
            mod.rewrite_sdk_ops('#define X86_HOST_API __declspec(dllimport)')
    def test_requires_generated_partitions(self):
        with tempfile.TemporaryDirectory() as root:
            base=pathlib.Path(root)
            gen=base/"generated"
            sdk=base/"sdk"/"runtime"/"cpu"
            gen.mkdir()
            sdk.mkdir(parents=True)
            (gen/"x86_func_mapping.cpp").write_text('bad')
            (gen/"x86_recomp_shared.h").write_text('test')
            (sdk/"x86_context.h").write_text('test')
            (sdk/"x86_ops.h").write_text('test')
            with self.assertRaises(ValueError):
                mod.prepare(gen,base/"sdk",base/"out")

if __name__ == "__main__":
    unittest.main()
