"""Render the accepted SVG assets into 256px alpha masks (resvg-py + Pillow)."""
from pathlib import Path
from io import BytesIO
import resvg_py
from PIL import Image
root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parents[1] / 'main/icons.h'
parts = ['// Generated from spec/icons by tools/make_icons.py.\n#pragma once\n#include <stdint.h>\n']
for name in ['v60', 'switch', 'aeropress', 'hoop']:
    svg = (root / f'spec/icons/{name}.svg').read_text().replace('currentColor', '#ffffff')
    svg = svg.replace('<svg ', '<svg width="256" height="256" ')
    data = resvg_py.svg_to_bytes(svg_string=svg)
    image = Image.open(BytesIO(data)).convert('RGBA')
    values = list(image.getchannel('A').getdata())
    parts.append(f'static const uint8_t icon_{name}[65536]={{\n')
    for i in range(0,len(values),64):parts.append(','.join(map(str,values[i:i+64]))+',\n')
    parts.append('};\n')
parts.append('static const uint8_t *const brewer_icons[]={icon_v60,icon_switch,icon_aeropress,icon_hoop};\n')
out.write_text(''.join(parts))
