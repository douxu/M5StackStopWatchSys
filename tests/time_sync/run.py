#!/usr/bin/env python3
"""Compile production time sync against deterministic ESP-IDF fakes."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="stopwatch-time-sync-") as directory:
    build = Path(directory)
    source = (root / "main/hal/hal_time_sync.cpp").read_text()
    source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
    (build / "time_sync_under_test.inc").write_text(source)
    binary = build / "time_sync_test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(build), str(root / "tests/time_sync/service_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
