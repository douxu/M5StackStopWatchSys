"""Generate two-level city choices from the MCA 2024 HTML table (UTF-8).
Usage: python3 scripts/generate_weather_regions.py /path/to/202401xzqh.html
Source: https://www.mca.gov.cn/mzsj/xzqh/2025/202401xzqh.html
"""
from pathlib import Path
import re, html, json, sys
root = Path(__file__).resolve().parents[1]
rows=[]
for row in re.findall(r'<tr\b[^>]*>(.*?)</tr>', Path(sys.argv[1]).read_text(), re.S|re.I):
    cells=[re.sub(r'\s+', '', html.unescape(re.sub(r'<[^>]+>', '', x))) for x in re.findall(r'<td\b[^>]*>(.*?)</td>', row, re.S|re.I)]
    for i,c in enumerate(cells[:-1]):
        if re.fullmatch(r'\d{6}', c):
            rows.append((c,cells[i+1].rstrip('*')))
provinces=[(c,n) for c,n in rows if c.endswith('0000')]
assert len(provinces)==34, len(provinces)
entries=[]
for code,name in provinces:
    if code[:2] in ('11','12','31','50','71','81','82'):
        # Municipalities/SARs have one weather location. Taiwan city names
        # supplement the MCA province-only entry from the China government
        # overview: https://www.gov.cn/guoqing/2020-07/28/content_5530577.htm
        # 9000001..9000009 are APP IDs, never official division codes.
        cities=[(code,name)]
        if code[:2]=='71':
            cities=[(str(9000001+i),n+'市') for i,n in enumerate(
                ['台北','新北','桃园','台中','台南','高雄','基隆','新竹','嘉义'])]
    else:
        cities=[(c,n) for c,n in rows if c[:2]==code[:2] and c.endswith('00') and not c.endswith('0000')]
        # Directly administered county-level cities have no prefecture parent.
        cities += [(c,n) for c,n in rows if c[:4] in (code[:2]+'90',code[:2]+'91') and not c.endswith('00')]
    entries.append(dict(code=code,name=name,cities=[dict(code=c,name=n) for c,n in cities]))
(root/'components/weather/regions.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2)+'\n')
lines=['// Generated from MCA 2024; see REGIONS.md. Do not edit by hand.','#include "regions.h"','namespace weather {','const Region provinces[] = {']
lines += ['    {%s, %s},'%(p['code'],json.dumps(p['name'],ensure_ascii=False)) for p in entries]
lines+=['};','const size_t provinceCount = sizeof(provinces)/sizeof(provinces[0]);','const City cities[] = {']
for p in entries:
    lines += ['    {%s, %s, %s},'%(c['code'],p['code'],json.dumps(c['name'],ensure_ascii=False)) for c in p['cities']]
lines+=['};','const size_t cityCount = sizeof(cities)/sizeof(cities[0]);','}']
(root/'components/weather/regions.cpp').write_text('\n'.join(lines)+'\n')
chars=''.join(sorted(set(''.join(n for c,n in rows if c.endswith('00'))+''.join(c['name'] for p in entries for c in p['cities']))))
(root/'components/weather/region_glyphs.txt').write_text(chars)
print(len(provinces),sum(len(p['cities']) for p in entries),'cities;',len(chars),'glyphs')
