#!/usr/bin/env python3
"""Streaming bounded mobile diagnostic report."""
import collections
import re
import sys

START, TAIL, EVENTS = 1200, 4096, 2048
FRAME = re.compile(rb"^D3D FRAME CHECKSUM frame=(\d+)/")
SWAP = re.compile(rb"^D3D ORIGINAL SWAP OFFSCREEN PASS: presents=(\d+)")
ROUTINE = (b"D3D LIVE ", b"D3D DRAW ", b"D3D SHADER ", b"D3D DEVICE ",
           b"D3D TEXTURE ", b"D3D STREAM ", b"D3D VB ", b"D3D VIEWPORT WRITER ",
           b"D3D8 GUEST BOUNDARY:", b"D3D FIRST DRAW ", b"D3D ORIGINAL ",
           b"D3D TEXTURED DRAW PASS", b"HULK DSOUND CALL ", b"KERNEL POINTER WAIT ",
           b"DISC READ ", b"D3D FRAME CHECKSUM ")
IMPORTANT = (b"failed", b"unsupported", b"error", b"fault", b"missing guest",
             b"budget", b"summary", b"rejected", b"exception", b"stopping")

def compact(source, out):
    tail = collections.deque(maxlen=TAIL)
    events = collections.deque(maxlen=EVENTS)
    counters = collections.Counter()
    n = 0
    last_swap = last_frame = None
    for raw in source:
        n += 1
        line = raw[:4096].rstrip(b"\r\n") + b"\n"
        if len(raw) > 4096:
            line = line.rstrip() + b" [line truncated]\n"
        if n <= START:
            out.write(line)
        else:
            tail.append((n, line))
        f, s = FRAME.match(line), SWAP.match(line)
        if f:
            last_frame = n, line
            counters["frames"] += 1
        if s:
            last_swap = n, line
            counters["swaps"] += 1
        if line.startswith(b"D3D FIRST DRAW "): counters["draw_attempts"] += 1
        if line.startswith(b"DISC READ "):
            counters["disc_reads"] += 1
            if b"status=00000000" in line: counters["disc_reads_ok"] += 1
        if line.startswith(b"D3D LIVE SETTER "): counters["setters"] += 1
        if n <= START: continue
        if f or s:
            if int((f or s).group(1)) % 120 == 0: events.append((n, line))
        elif any(key in line.lower() for key in IMPORTANT) or not line.startswith(ROUTINE):
            events.append((n, line))
    out.write(("\nHULK COMPACT REPORT original_lines=%d counters=%s\n" % (n, dict(counters))).encode())
    out.write(b"Selected events and final raw context:\n")
    retained = dict(events)
    retained.update(tail)
    for item in (last_swap, last_frame):
        if item and item[0] > START: retained[item[0]] = item[1]
    previous = START
    for i, line in sorted(retained.items()):
        if i > previous+1: out.write(("[omitted %d lines]\n" % (i-previous-1)).encode())
        out.write(line)
        previous = i
    out.flush()

def self_test():
    import io
    data = (b"startup\n"*START + b"D3D LIVE PUSH method=1710 value=00000000\n"*50000 +
            b"D3D ORIGINAL SWAP OFFSCREEN PASS: presents=120 draws=240\n" +
            b"D3D FRAME CHECKSUM frame=120/5400 hash=ABCD\n" +
            b"D3D FIRST DRAW caller=1234\n" + b"D3D DRAW TSS 0 1=00000001\n"*500 +
            b"Guest thread failed: Unsupported original texture resource\n")
    out = io.BytesIO()
    compact(io.BytesIO(data), out)
    result = out.getvalue()
    assert len(result) < len(data)//5
    assert b"startup" in result and b"original_lines=51704" in result
    assert b"presents=120" in result and b"caller=1234" in result
    assert b"Unsupported original texture resource" in result

if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]: self_test()
    else: compact(sys.stdin.buffer, sys.stdout.buffer)
