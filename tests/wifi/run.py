#!/usr/bin/env python3
"""Run the actual HAL controller with deterministic host-side ESP-IDF fakes."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="stopwatch-wifi-") as directory:
    build = Path(directory)
    hal = (root / "main/hal/hal.h").read_text()
    api = hal.split("/* ---------------------------------- Wi-Fi --------------------------------- */")[1].split("/* ---------------------------------- Badge")[0]
    (build / "hal_api.inc").write_text("class Hal { public:\n" + api + "\nvoid wifi_init(); void wifi_tick(); void wifi_enter_badge_ap(); void wifi_exit_badge_ap();\n};\n")
    source = (root / "main/hal/hal_network.cpp").read_text()
    # Only replace platform includes; the controller and its public API are the
    # production code. The firmware build separately validates real IDF types.
    source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
    (build / "hal_network_under_test.inc").write_text(source)
    binary = build / "network_test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(build), str(root / "tests/wifi/network_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
