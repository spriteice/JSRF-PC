#!/usr/bin/env python3
"""Find the game's button icons in its own files, for the button prompts'
table (src/nv2a_gl/nv2a_prompts.c).

    python3 tools/prompt_census.py list MEDIA [FILE ...]
    python3 tools/prompt_census.py find MEDIA [FILE ...]
    python3 tools/prompt_census.py show MEDIA FILE OFFSET X Y W H [--step N]
    python3 tools/prompt_census.py sprites MEDIA FILE OFFSET [X Y W H] [--least N]

MEDIA is the game's Media folder, on the machine that has the game; FILE is
a path under it (Disp/SprNorm.dat), and without one every .dat file under
it is read. Nothing is written anywhere, and no pixel leaves as a picture:

- list: every texture in each file's containers (MULT and NORM, the game's
  own), one line each: the file, the texture's offset in it, its size and
  format, its head, the CRC-32 of its first level's first 1024 bytes, and
  its key, the CRC-32 of all of its first level's bytes, as the renderer's
  table names it.
- find: the textures with tiles in the pad's four face colours, A's green,
  B's red, X's blue and Y's yellow, and each tile's rectangle, to be read
  with show and named by hand.
- show: a rectangle of one texture as text, a character for each step x
  step square of pixels: G R B Y for the four colours (lower case where
  their brightest channel is below 200), and for the rest its brightness (`.` `-` `+` `#`), blank where it
  is clear.
- sprites: the separate pictures in a texture, or in a rectangle of it:
  each area of texels that aren't clear, joined where they touch (a
  diagonal too), as its rectangle and its count of texels, top to bottom.
  It finds the icons with no face colour (the triggers, Start, Back), to
  be read with show and named by hand.

Only DXT3 (the game's format byte 6) is decoded; a texture in another
format is listed with its byte and not looked into. It needs only Python's
standard library. Its test, tools/test_prompt_census.py, builds its files
in the game's container format with made-up textures.
"""
import argparse
import pathlib
import struct
import sys
import zlib

HEADER = 0x20                   # a texture item's header, before its texels
FORMATS = {6: "DXT3"}           # the game's format byte: the ones decoded
DXT3_BLOCK = 16                 # bytes for 4 x 4 texels
CONTAINERS = (b"MULT", b"NORM")
MAX_SIDE = 4096


def items(buf, base=0):
    """(offset, size) of each item in the container at base, the items of
    any container in it in its place. A container is its magic, its count
    and 8 bytes, then for each item 16 bytes (where it starts and where the
    next entry is, each from base, and its size) and the item."""
    if buf[base:base + 4] not in CONTAINERS or base + 16 > len(buf):
        return []
    count, = struct.unpack_from("<I", buf, base + 4)
    out, entry = [], base + 16
    for _ in range(count):
        if entry + 16 > len(buf):
            break
        start, nxt, size = struct.unpack_from("<3I", buf, entry)
        at = base + start
        if at + size > len(buf) or nxt == 0:
            break
        if buf[at:at + 4] in CONTAINERS:
            out += items(buf, at)
        else:
            out.append((at, size))
        entry = base + nxt
    return out


def texture(buf, at, size):
    """A texture item's (width, height, format byte, levels) from its header,
    or None for an item that isn't one. Its height is what its first level's
    bytes leave, as the header gives only its width."""
    if size <= HEADER or buf[at:at + 4].isalpha():
        return None
    width, = struct.unpack_from("<I", buf, at + 0x14)
    fmt, levels = buf[at + 0x18], buf[at + 0x19]
    if not width or width > MAX_SIDE or width & (width - 1) or not levels:
        return None
    if fmt not in FORMATS:
        return width, 0, fmt, levels
    data = size - HEADER
    if levels == 1:
        height = data // width
    else:
        height = width              # a chain: square, as the game's are
    if not height or height > MAX_SIDE or width * height > data:
        return None
    return width, height, fmt, levels


HEAD = 1024                     # the bytes the head covers (NV2A_PROMPT_HEAD_BYTES)


def key(buf, at, width, height):
    """The renderer's key for the texture: the CRC-32 of its first level."""
    return zlib.crc32(buf[at + HEADER:at + HEADER + width * height]) & 0xFFFFFFFF


def head(buf, at, width, height):
    """The renderer's quick check: the CRC-32 of its first level's first
    HEAD bytes, or all of them where it has fewer."""
    return zlib.crc32(buf[at + HEADER:at + HEADER + min(width * height, HEAD)]) & 0xFFFFFFFF


def textures(buf):
    """(offset, width, height, format byte, levels) of each texture."""
    out = []
    for at, size in items(buf):
        t = texture(buf, at, size)
        if t:
            out.append((at,) + t)
    return out


def _c565(c):
    return (((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)


def dxt3(buf, at, width, height):
    """The first level as rows of (r, g, b, a)."""
    px = [[None] * width for _ in range(height)]
    p = at + HEADER
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            alpha = buf[p:p + 8]
            c0, c1, bits = struct.unpack_from("<HHI", buf, p + 8)
            a, b = _c565(c0), _c565(c1)
            pal = (a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)),
                   tuple((x + 2 * y) // 3 for x, y in zip(a, b)))
            for i in range(16):
                al = ((alpha[i // 2] >> (4 * (i & 1))) & 15) * 17
                px[by + i // 4][bx + i % 4] = pal[(bits >> (2 * i)) & 3] + (al,)
            p += DXT3_BLOCK
    return px


def colour(r, g, b, a):
    """'G', 'R', 'B' or 'Y' for the pad's four face colours, else None."""
    if a <= 128:
        return None
    if g > 150 and r < 120 and b < 120:
        return "G"
    if r > 150 and g < 90 and b < 90:
        return "R"
    if b > 150 and r < 100 and g < 160:
        return "B"
    if r > 170 and g > 150 and b < 90:
        return "Y"
    return None


def tiles(px, least=64):
    """Rectangles (x, y, w, h, colour) of the face colours' connected areas
    of at least `least` pixels, largest first."""
    height, width = len(px), len(px[0])
    seen = [[False] * width for _ in range(height)]
    out = []
    for y in range(height):
        for x in range(width):
            c = colour(*px[y][x])
            if not c or seen[y][x]:
                continue
            stack, n = [(x, y)], 0
            seen[y][x] = True
            x0 = x1 = x
            y0 = y1 = y
            while stack:
                cx, cy = stack.pop()
                n += 1
                x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < width and 0 <= ny < height and not seen[ny][nx] \
                            and colour(*px[ny][nx]) == c:
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            if n >= least:
                out.append((x0, y0, x1 - x0 + 1, y1 - y0 + 1, c, n))
    out.sort(key=lambda t: -t[5])
    return [t[:5] for t in out]


def sprites(px, x, y, w, h, least=16):
    """Rectangles (x, y, w, h, texels) of the areas of texels that aren't
    clear (alpha above 128) in the rectangle, each joined to the texels it
    touches, a diagonal too; those of at least `least` texels, by their top
    and then their left."""
    seen = set()
    out = []
    for sy in range(y, y + h):
        for sx in range(x, x + w):
            if (sx, sy) in seen or px[sy][sx][3] <= 128:
                continue
            stack, n = [(sx, sy)], 0
            seen.add((sx, sy))
            x0 = x1 = sx
            y0 = y1 = sy
            while stack:
                cx, cy = stack.pop()
                n += 1
                x0, x1, y0, y1 = min(x0, cx), max(x1, cx), min(y0, cy), max(y1, cy)
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if x <= nx < x + w and y <= ny < y + h and (nx, ny) not in seen \
                                and px[ny][nx][3] > 128:
                            seen.add((nx, ny))
                            stack.append((nx, ny))
            if n >= least:
                out.append((x0, y0, x1 - x0 + 1, y1 - y0 + 1, n))
    out.sort(key=lambda t: (t[1], t[0]))
    return out


def show(px, x, y, w, h, step=1):
    """The rectangle as text, with a ruler of x every 10 columns."""
    lines = ["     " + "".join(str((cx // 10) % 10) if (cx - x) % (10 * step) == 0 else " "
                             for cx in range(x, x + w, step))]
    for cy in range(y, y + h, step):
        line = f"{cy:4d} "
        for cx in range(x, x + w, step):
            cell = [px[j][i] for j in range(cy, min(cy + step, y + h))
                    for i in range(cx, min(cx + step, x + w))]
            alpha = sum(p[3] for p in cell) / len(cell)
            if alpha < 64:
                line += " "
                continue
            counts = {}
            for p in cell:
                c = colour(*p)
                if c:
                    counts[c] = counts.get(c, 0) + 1
            lum = sum((p[0] + p[1] + p[2]) / 3 for p in cell) / len(cell)
            peak = sum(max(p[:3]) for p in cell) / len(cell)
            best = max(counts, key=counts.get) if counts else None
            if best and counts[best] * 2 >= len(cell):
                line += best.lower() if peak < 200 else best
            else:
                line += "#" if lum > 200 else "+" if lum > 120 else "-" if lum > 60 else "."
        lines.append(line)
    return "\n".join(lines)


def files(media, names):
    media = pathlib.Path(media)
    if names:
        return [media / n for n in names]
    return sorted(p for p in media.rglob("*") if p.suffix.lower() == ".dat" and p.is_file())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("list", "find"):
        p = sub.add_parser(name)
        p.add_argument("media")
        p.add_argument("files", nargs="*")
    p = sub.add_parser("show")
    p.add_argument("media")
    p.add_argument("file")
    p.add_argument("offset", type=lambda s: int(s, 0))
    for n in ("x", "y", "w", "h"):
        p.add_argument(n, type=int)
    p.add_argument("--step", type=int, default=1)
    p = sub.add_parser("sprites")
    p.add_argument("media")
    p.add_argument("file")
    p.add_argument("offset", type=lambda s: int(s, 0))
    p.add_argument("rect", type=int, nargs="*", metavar="X Y W H")
    p.add_argument("--least", type=int, default=16)
    a = ap.parse_args(argv)
    media = pathlib.Path(a.media)

    if a.cmd in ("show", "sprites"):
        buf = (media / a.file).read_bytes()
        sizes = dict(items(buf))
        t = texture(buf, a.offset, sizes[a.offset]) if a.offset in sizes else None
        if not t or t[2] not in FORMATS:
            sys.exit(f"{a.file}: no texture this decodes at {a.offset:#x}")
        width, height = t[0], t[1]
        if a.cmd == "sprites":
            if len(a.rect) not in (0, 4):
                sys.exit("sprites: give the rectangle as X Y W H, or none for all of it")
            a.x, a.y, a.w, a.h = a.rect or (0, 0, width, height)
        if a.x < 0 or a.y < 0 or a.x + a.w > width or a.y + a.h > height:
            sys.exit(f"{a.file}: the rectangle is outside its {width} x {height}")
        px = dxt3(buf, a.offset, width, height)
        if a.cmd == "show":
            print(show(px, a.x, a.y, a.w, a.h, a.step))
        else:
            for x, y, w, h, n in sprites(px, a.x, a.y, a.w, a.h, a.least):
                print(f"    {x},{y} {w}x{h} {n} texels")
        return 0

    for path in files(media, a.files):
        buf = path.read_bytes()
        rel = path.relative_to(media).as_posix()
        for at, width, height, fmt, levels in textures(buf):
            if fmt not in FORMATS:
                if a.cmd == "list":
                    print(f"{rel} {at:#x} {width} format byte {fmt}, not decoded")
                continue
            line = (f"{rel} {at:#x} {width}x{height} {FORMATS[fmt]} levels {levels} "
                    f"head {head(buf, at, width, height):08X} key {key(buf, at, width, height):08X}")
            if a.cmd == "list":
                print(line)
                continue
            found = tiles(dxt3(buf, at, width, height))
            if len({t[4] for t in found}) == 4:
                print(line)
                for x, y, w, h, c in found:
                    print(f"    {c} {x},{y} {w}x{h}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
