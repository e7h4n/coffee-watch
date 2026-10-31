"""Extract a complete SCREEN565 framebuffer emitted by the physical demo."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--frame', type=int, default=-1)
    args = parser.parse_args()
    frames = []
    current = None
    for raw in args.capture.open():
        line = json.loads(raw).get('line', '')
        if line.startswith('SCREEN565 '):
            _, width, height = line.split()
            current = (int(width), int(height), {})
        elif line.startswith('PX:') and current:
            _, y, data = line.split(':', 2)
            if len(data) != current[0] * 4:
                raise SystemExit(f'invalid screenshot row {y}')
            current[2][int(y)] = data
        elif line == 'SCREEN_END' and current:
            if len(current[2]) != current[1]:
                raise SystemExit('incomplete screenshot')
            frames.append(current)
            current = None
    if not frames:
        raise SystemExit('no complete framebuffer in capture')
    from PIL import Image
    width, height, rows = frames[args.frame]
    pixels = bytearray()
    for y in range(height):
        data = rows[y]
        for x in range(width):
            value = int(data[x*4:x*4+4], 16)
            pixels.extend((((value>>11)&31)*255//31, ((value>>5)&63)*255//63, (value&31)*255//31))
    image = Image.frombytes('RGB', (width, height), bytes(pixels))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f'saved frame {args.frame}: {width}x{height} -> {args.output}')


if __name__ == '__main__':
    main()
