#!/usr/bin/env python3
"""Generates the sample files and checks that every loader agrees on their content.

Usage: check_samples.py VOXINFO_BINARY WORK_DIR
"""
import os
import re
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
voxinfo, work = sys.argv[1], sys.argv[2]
subprocess.run([sys.executable, os.path.join(here, "make_samples.py"), work], check=True)


def info(name):
    out = subprocess.run([voxinfo, os.path.join(work, name)], check=True, capture_output=True, text=True).stdout
    size = re.search(r"size:\s+(\d+) x (\d+) x (\d+)", out).groups()
    voxels = int(re.search(r"voxels:\s+(\d+)", out).group(1))
    quads = re.search(r"quads:\s+(\d+) opaque, (\d+) transparent", out).groups()
    top = sorted(re.findall(r"^\s+(minecraft:\S+|#\d+)\s+#([0-9A-F]{6})\s+(\d+)$", out, re.M))
    return tuple(map(int, size)), voxels, tuple(map(int, quads)), top


failures = 0


def check(cond, msg):
    global failures
    print(("ok   " if cond else "FAIL ") + msg)
    failures += 0 if cond else 1


ref = info("house.schem")
check(ref[0] == (13, 10, 11) and ref[1] == 539, "house.schem size/voxels: %s" % (ref[:2],))
for f in ["house_v3.schem", "house.schematic", "house.litematic", "house.nbt"]:
    got = info(f)
    check(got == ref, "%s matches house.schem" % f)

scene = info("scene.vox")
check(scene[0] == (13, 5, 8) and scene[1] == 88, "scene.vox size/voxels: %s" % (scene[:2],))
check(scene[2][1] > 0, "scene.vox glass material is transparent")

# Corrupt input must fail cleanly, not crash.
bad = os.path.join(work, "truncated.schem")
with open(os.path.join(work, "house.schem"), "rb") as f:
    data = f.read()
with open(bad, "wb") as f:
    f.write(data[: len(data) // 2])
r = subprocess.run([voxinfo, bad], capture_output=True, text=True)
check(r.returncode == 1 and "error" in r.stderr, "truncated file reports an error")

sys.exit(1 if failures else 0)
