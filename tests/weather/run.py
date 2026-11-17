#!/usr/bin/env python3
from pathlib import Path
import subprocess, os, sys
root=Path(__file__).resolve().parents[2]
build=Path('/tmp/stopwatch-weather-tests'); build.mkdir(exist_ok=True)
idf=Path(os.environ.get('IDF_PATH','/Users/douxu/.espressif/v5.5.4/esp-idf'))
cjson=idf/'components/json/cJSON'
subprocess.run(['cc','-c',str(cjson/'cJSON.c'),'-o',str(build/'json.o')],check=True)
inc=['-I',str(root/'components/weather/include'),'-I',str(cjson)]
subprocess.run(['c++','-std=c++17',*inc,str(root/'tests/weather/parser_test.cpp'),str(root/'components/weather/weather_parse.cpp'),str(build/'json.o'),'-o',str(build/'parser')],check=True)
subprocess.run([str(build/'parser'),*sys.argv[1:]],check=True)
source=(root/'components/weather/weather.cpp').read_text()
for header in ['#include <hal/hal.h>','#include <hal/utils/settings/settings.h>','#include <esp_http_client.h>','#include <esp_crt_bundle.h>','#include <freertos/FreeRTOS.h>','#include <freertos/task.h>']:
    source=source.replace(header,'')
(build/'service_under_test.inc').write_text(source)
subprocess.run(['c++','-std=c++17',*inc,'-I',str(build),str(root/'tests/weather/service_test.cpp'),str(root/'components/weather/weather_parse.cpp'),str(root/'components/weather/regions.cpp'),str(build/'json.o'),'-o',str(build/'service')],check=True)
subprocess.run([str(build/'service'),str(root/'tests/weather/beijing_fixture.json')],check=True)
config=root/'tests/weather/lv_conf.h'
subprocess.run(['cmake','-S',str(root/'components/lvgl'),'-B',str(build/'lvgl'),'-DLV_BUILD_CONF_PATH='+str(config),'-DCONFIG_LV_BUILD_DEMOS=OFF','-DCONFIG_LV_BUILD_EXAMPLES=OFF','-DCONFIG_LV_USE_THORVG_INTERNAL=OFF'],check=True)
subprocess.run(['cmake','--build',str(build/'lvgl'),'-j','8'],check=True)
flags=['-I',str(root/'main/apps/common/standby'),'-I',str(root/'components/codex_usage/include'),'-DLV_LVGL_H_INCLUDE_SIMPLE',*inc,'-I',str(root/'components/lvgl'),'-I',str(root/'main'),'-I',str(build),'-DLV_CONF_PATH="'+str(config)+'"']
subprocess.run(['cc',*flags,'-c',str(root/'main/assets/fonts/weather_city_24.c'),'-o',str(build/'font.o')],check=True)
subprocess.run(['cc',*flags,'-c',str(root/'main/assets/fonts/lv_font_maple_mono_medium_48.c'),'-o',str(build/'clock_font.o')],check=True)
for src,out in [('main/apps/common/standby/standby.cpp','standby_under_test.inc'),('main/apps/app_setup/workers/standby_display.cpp','view_under_test.inc')]:
    text=(root/src).read_text()
    for header in ['#include <hal/hal.h>','#include <assets/assets.h>','#include <driver/gpio.h>','#include "workers.h"']:
        text=text.replace(header,'')
    (build/out).write_text(text)
lib=next((build/'lvgl').rglob('liblvgl.a'))
subprocess.run(['c++','-std=c++17',*flags,str(root/'tests/weather/ui_test.cpp'),str(root/'components/weather/regions.cpp'),str(root/'components/weather/weather_parse.cpp'),str(root/'components/codex_usage/codex_usage_parse.cpp'),str(build/'font.o'),str(build/'clock_font.o'),str(build/'json.o'),str(lib),'-o',str(build/'ui')],check=True)
subprocess.run([str(build/'ui')],cwd=build,check=True)
print('Previews:',build)
