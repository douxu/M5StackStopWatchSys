#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
idf = Path(os.environ.get("IDF_PATH", "/Users/douxu/.espressif/v5.5.4/esp-idf"))
cjson = idf / "components/json/cJSON"
with tempfile.TemporaryDirectory(prefix="stopwatch-codex-tests-") as directory:
    build = Path(directory)
    subprocess.run(["cc", "-c", str(cjson / "cJSON.c"), "-o", str(build / "json.o")], check=True)
    subprocess.run(["c++", "-std=c++17", "-I", str(cjson), "-I", str(root / "components/codex_usage/include"),
                    str(root / "tests/codex_usage/parser_test.cpp"), str(root / "components/codex_usage/codex_usage_parse.cpp"),
                    str(build / "json.o"), "-o", str(build / "parser")], check=True)
    subprocess.run([str(build / "parser")], check=True)
    source = (root / "components/codex_usage/codex_usage.cpp").read_text()
    for header in ("esp_http_server.h", "esp_random.h",
                   "freertos/FreeRTOS.h", "freertos/task.h"):
        source = source.replace(f"#include <{header}>", "")
    (build / "service_under_test.inc").write_text(source)
    subprocess.run(["c++", "-std=c++17", "-I", str(cjson), "-I", str(root / "components/codex_usage/include"),
                    "-I", str(root / "main"), "-I", str(root / "components/weather/include"), "-I", str(build),
                    str(root / "tests/codex_usage/service_test.cpp"), str(root / "components/codex_usage/codex_usage_parse.cpp"),
                    str(build / "json.o"), "-o", str(build / "service")], check=True)
    subprocess.run([str(build / "service")], check=True)
subprocess.run([sys.executable, "-B", str(root / "tests/codex_usage/bridge_test.py")], check=True)
