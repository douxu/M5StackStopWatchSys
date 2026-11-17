#!/usr/bin/env python3
"""Build actual LVGL/UI on the host; run interaction assertions and render PPMs.
Usage: python3 tests/wifi/run_ui.py /tmp/stopwatch-wifi-ui
Requires CMake and a native C/C++ compiler. No device or network required.
"""
from pathlib import Path
import os
import subprocess
import sys
root = Path(__file__).resolve().parents[2]
build = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/stopwatch-wifi-ui").resolve()
build.mkdir(parents=True, exist_ok=True)
config = root / "tests/wifi/lv_conf.h"
hal = (root / "main/hal/hal.h").read_text()
api = hal.split("/* ---------------------------------- Wi-Fi --------------------------------- */")[1].split("/* ---------------------------------- Badge")[0]
(build / "hal_api.inc").write_text("class Hal { public:\n" + api + "\n};\n")
source = (root / "main/apps/app_setup/workers/wifi_scan.cpp").read_text()
source = source.replace('#include "workers.h"', '').replace('#include <assets/assets.h>', '')
(build / "wifi_scan_under_test.inc").write_text(source)
subprocess.run(["cmake", "-S", str(root / "components/lvgl"), "-B", str(build / "lvgl"),
                "-DLV_BUILD_CONF_PATH=" + str(config), "-DCONFIG_LV_BUILD_DEMOS=OFF",
                "-DCONFIG_LV_BUILD_EXAMPLES=OFF", "-DCONFIG_LV_USE_THORVG_INTERNAL=OFF"], check=True)
subprocess.run(["cmake", "--build", str(build / "lvgl"), "-j", "8"], check=True)
flags = ["-I", str(root / "components/lvgl"), "-I", str(build), '-DLV_CONF_PATH="' + str(config) + '"']
font = build / "font.o"
subprocess.run([os.environ.get("CC", "cc"), *flags, "-c", str(root / "main/assets/fonts/MontserratSemiBold26.c"), "-o", str(font)], check=True)
lib = next((build / "lvgl").rglob("liblvgl.a"))
subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", *flags,
                str(root / "tests/wifi/ui_test.cpp"), str(font), str(lib), "-o", str(build / "ui_test")], check=True)
subprocess.run([str(build / "ui_test")], cwd=build, check=True)
print("Rendered previews:", build)
