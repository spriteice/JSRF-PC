"""Make tests/graffiti/pictures/, the small pictures run.py reads in each
format: made-up patterns of a few colours, so the readers can be checked
pixel by pixel. Run by hand when they need making again (it needs Pillow,
with WebP and AVIF); CI only reads the files, which are committed.

    python3 tests/graffiti/make_pictures.py
"""
import io
import pathlib

from PIL import Image

here = pathlib.Path(__file__).resolve().parent / "pictures"
here.mkdir(exist_ok=True)

RED, GREEN, BLUE, WHITE = (220, 30, 40, 255), (20, 200, 60, 255), (30, 40, 230, 255), (255, 255, 255, 255)
CLEAR = (0, 0, 0, 0)


def quarters(w, h, a, b, c, d):
    """a top left, b top right, c bottom left, d bottom right."""
    img = Image.new("RGBA", (w, h))
    for y in range(h):
        for x in range(w):
            img.putpixel((x, y), (a if x < w // 2 else b) if y < h // 2 else (c if x < w // 2 else d))
    return img


rgba = quarters(8, 6, RED, GREEN, BLUE, CLEAR)
rgba.save(here / "quarters.png")
rgba.save(here / "quarters.webp", lossless=True, exact=True)
rgba.save(here / "quarters.tif", compression="tiff_adobe_deflate")
rgba.save(here / "quarters-lzw.tif", compression="tiff_lzw")
rgba.convert("RGB").save(here / "quarters-rgb.tif", compression="packbits")

# 16 bits a channel: the readers take it to 8.
Image.new("I;16", (4, 4), 0x8080).save(here / "grey16.png")

# Lossy: big flat areas, read within a tolerance.
solid = quarters(32, 16, RED, GREEN, BLUE, WHITE).convert("RGB")
solid.save(here / "quarters.jpg", quality=95, subsampling=0)
quarters(32, 16, RED, GREEN, BLUE, CLEAR).save(here / "quarters.avif", quality=95)

# A phone's way: stored 32 x 16, red on the left, with EXIF orientation 6,
# so it shows 16 x 32, red at the top.
side = quarters(32, 16, RED, BLUE, RED, BLUE).convert("RGB")
exif = Image.Exif()
exif[0x0112] = 6
side.save(here / "turned.jpg", quality=95, subsampling=0, exif=exif.tobytes())

# An animation: its first frame is all green, its second all blue.
frames = [Image.new("RGBA", (8, 8), GREEN), Image.new("RGBA", (8, 8), BLUE)]
frames[0].save(here / "animated.webp", save_all=True, append_images=frames[1:], duration=100, lossless=True)

# SVG: a 40 x 20 drawing, red on the left half, the right half clear.
(here / "half.svg").write_text(
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<!-- made for JSRF-PC\'s tests -->\n'
    '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20" viewBox="0 0 40 20">\n'
    '  <rect x="0" y="0" width="20" height="20" fill="#dc1e28"/>\n'
    '</svg>\n', encoding="utf-8")

for p in sorted(here.iterdir()):
    print(f"{p.name}: {p.stat().st_size} bytes")
