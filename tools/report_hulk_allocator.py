#!/usr/bin/env python3
"""Summarize only local disassembly comments around the allocator caller."""
import collections
import pathlib
import re
import sys

def report(folder, log):
    m = re.search(r"HULK ALLOC caller=([0-9A-Fa-f]{8})", log)
    if not m:
        return []
    caller = int(m.group(1), 16)
    matches = []
    refs = []
    comment = re.compile(r"^\s*//\s+([0-9A-Fa-f]{8})\s+(.*)")
    function = re.compile(r"X86_FUNC\(([^)]+)\)")
    for path in sorted(folder.glob("x86_recomp.*.cpp")):
        history = collections.deque(maxlen=10)
        name = ""
        remaining = 0
        with path.open(encoding="utf-8", errors="replace") as src:
            for line in src:
                fn = function.search(line)
                if fn:
                    name = fn.group(1)
                    history.clear()
                    remaining = 0
                ins = comment.match(line)
                if not ins:
                    continue
                va = int(ins.group(1), 16)
                item = f"{ins.group(1)} {ins.group(2)}"
                if remaining:
                    matches.append(item)
                    remaining -= 1
                if va == caller:
                    matches.extend([f"Caller: {name} ({path.name})", *history, item])
                    remaining = 8
                if len(refs) < 20 and re.search(r"0x0*527C60\b|0x0*8D320\b", ins.group(2), re.I):
                    refs.append(f"{name}: {item}")
                history.append(item)
    return ["=== Allocator caller (local source) ===",
            *(matches or [f"No exact return instruction {caller:08X} found"]),
            "=== References to allocator table/stop routine (bounded) ===", *refs]

def main():
    if sys.argv[1:] == ["--self-test"]:
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp)
            (path/"x86_recomp.0.cpp").write_text(
                "X86_FUNC(__imp__sub_00100000) {\n // 00100000 call 0x00266E00\n"
                " // 00100005 mov ecx, [0x00527C60]\n // 0010000A ret\n}\n")
            result = "\n".join(report(path, "HULK ALLOC caller=00100005 type=0"))
            assert "00100000 call" in result and "0010000A ret" in result and "References" in result
        return
    if len(sys.argv) != 3:
        raise SystemExit("usage: report_hulk_allocator.py GENERATED LOG")
    for line in report(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]).read_text(errors="replace")):
        print(line)

if __name__ == "__main__":
    main()
