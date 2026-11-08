#!/usr/bin/env python3
"""Paint GFOpenLayout draw ops to a PNG. The positions are the engine's."""
import sys
from PIL import Image, ImageDraw, ImageFont

SANS = "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
BOLD = "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"


def font(px, bold=False):
    px = max(8, int(px))
    return ImageFont.truetype(BOLD if bold else SANS, px)


def argb(n):
    n &= 0xFFFFFFFF
    return ((n >> 16) & 255, (n >> 8) & 255, n & 255, (n >> 24) & 255)


def paint_op(base, clip, fn):
    layer = Image.new("RGBA", base.size, (0, 0, 0, 0))
    fn(ImageDraw.Draw(layer))
    if clip and clip[2] > clip[0] and clip[3] > clip[1]:
        kept = Image.new("RGBA", base.size, (0, 0, 0, 0))
        kept.paste(layer.crop(clip), (clip[0], clip[1]))
        layer = kept
    out = Image.alpha_composite(base, layer)
    base.paste(out)


def main():
    ops = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
    w, h = 720, 460
    im = Image.new("RGBA", (w, h), (232, 232, 232, 255))
    clip = None
    for line in ops:
        if line.startswith("W "):
            w, h = map(int, line.split()[1:])
            im = Image.new("RGBA", (w, h), (232, 232, 232, 255))
        elif line.startswith("C "):
            x, y, cw, ch = map(int, line.split()[1:])
            clip = None if cw <= 0 or ch <= 0 else (x, y, x + cw, y + ch)
        elif line.startswith("B "):
            p = line.split()
            x, y, bw, bh, n = map(int, p[1:6])
            if bw <= 0 or bh <= 0:
                continue
            rest = p[6:]
            stops = []
            for i in range(n):
                stops.append((int(rest[i * 2], 16), int(rest[i * 2 + 1])))
            border = int(rest[n * 2], 16) if rest else 0
            br = int(rest[n * 2 + 1]) if len(rest) > n * 2 + 1 else 0
            rad = int(rest[n * 2 + 2]) if len(rest) > n * 2 + 2 else 0

            def box(d, x=x, y=y, bw=bw, bh=bh, stops=stops, br=br, border=border, rad=rad):
                rect = [x, y, x + bw - 1, y + bh - 1]
                if stops:
                    c = argb(stops[0][0])
                    if len(stops) == 1:
                        if rad:
                            d.rounded_rectangle(rect, radius=rad, fill=c)
                        else:
                            d.rectangle(rect, fill=c)
                    else:
                        c0, c1 = argb(stops[0][0]), argb(stops[-1][0])
                        for yy in range(y, y + bh):
                            t = (yy - y) / max(1, bh - 1)
                            col = tuple(int(c0[k] + (c1[k] - c0[k]) * t) for k in range(4))
                            d.line((x, yy, x + bw - 1, yy), fill=col)
                if br and (border & 0xFF000000):
                    d.rectangle(rect, outline=argb(border))

            paint_op(im, clip, box)
        elif line.startswith("T "):
            parts = line.split(" ", 6)
            if len(parts) < 7:
                continue
            x, base, px, col, alpha = int(parts[1]), int(parts[2]), int(parts[3]), int(parts[4], 16), int(parts[5])
            text = parts[6]
            if alpha < 10 or not text:
                continue
            f = font(px)
            c = argb(col)

            def text_op(d, x=x, base=base, px=px, text=text, f=f, c=c):
                d.text((x, base - int(px * 0.8)), text, font=f, fill=c)

            paint_op(im, clip, text_op)
        elif line.startswith("M "):
            x, y, mw, mh, kind, col, alpha = line.split()[1:]
            x, y, mw, mh, kind = map(int, (x, y, mw, mh, kind))
            c = argb(int(col, 16))
            cx, cy = x + mw // 2, y + mh // 2

            def mark(d, x=x, y=y, mw=mw, mh=mh, kind=kind, c=c, cx=cx, cy=cy):
                if kind == 1:
                    d.line((x + 2, cy, cx, y + mh - 2), fill=c, width=2)
                    d.line((cx, y + mh - 2, x + mw - 2, y + 2), fill=c, width=2)
                elif kind == 2:
                    d.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=c)
                elif kind in (6, 7, 8):
                    d.polygon([(cx - 4, cy - 2), (cx + 4, cy - 2), (cx, cy + 3)], fill=c)
                elif kind == 5:
                    d.line((cx, y + 1, cx, y + mh - 1), fill=c)

            paint_op(im, clip, mark)
        elif line.startswith("P "):
            x, y, pw, ph, path = line.split()[1:]
            x, y, pw, ph = map(int, (x, y, pw, ph))
            layer = Image.open(path).convert("RGBA")
            im.paste(layer, (x, y))
    im.convert("RGB").save(sys.argv[2], "PNG")


if __name__ == "__main__":
    main()
