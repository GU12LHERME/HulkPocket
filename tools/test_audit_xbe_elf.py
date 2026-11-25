import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location("audit_xbe_elf",Path(__file__).with_name("audit_xbe_elf.py"))
assert spec and spec.loader
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

class ImportAuditTests(unittest.TestCase):
    def test_symbol_parsing(self):
        lines=("                 U x86_icall(X86Context&, unsigned char*, unsigned int)\n"
               "other.o: U kernel::GuestCall()\n"
               "                 U operator new(unsigned long)\n"
               "00000000 T defined_symbol\n")
        self.assertEqual(mod.parse_undefined(lines),
            ["x86_icall(X86Context&, unsigned char*, unsigned int)",
             "kernel::GuestCall()","operator new(unsigned long)"])
    def test_classification(self):
        self.assertEqual(mod.classify("x86_icall(X86Context&)"),"guest_dispatch")
        self.assertEqual(mod.classify("x86_trap(X86Context&)"),"x86_runtime")
        self.assertEqual(mod.classify("kernel::R32(unsigned int)"),"guest_kernel_or_hook")
        self.assertEqual(mod.classify("mem::IsCommitted(unsigned int)"),"guest_kernel_or_hook")
        self.assertEqual(mod.classify("memcpy"),"cxx_or_platform")

if __name__=="__main__":
    unittest.main()
