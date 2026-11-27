"""The app icon: JSRF in white, stacked as JS over RF, on a vivid green.

The letters are set in Nise JSRF, a fan-made font by Yoyo, free to use
("100% Free" on dafont.com, where it is found as nisejsrf). The font file
is not kept in the repository: it is given to this script by path, and
only the icons it writes are. The green is a colour picked in the spirit
of the game's palette, not taken from its files. Stacked, the letters stay
big enough to read at 24 pixels: at 32 pixels and below they are drawn a
little bolder, and without the shadow, which would only blur them there.

Writes both icons, from the one drawing:
  launcher/icon/jsrf-pc.png   256 pixels square, the icon the Linux
                              packages install (launcher/linux/package.sh,
                              launcher/firststart/package.sh);
  launcher/windows/jsrf.ico   16, 24, 32, 48, 64, 128 and 256 pixels, the
                              icon resources.rc gives the launcher and the
                              engine.

  python3 launcher/icon/make_icon.py NiseJSRF.TTF [png] [ico]
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

GREEN  = (122, 204,  26)
SHADOW = ( 84, 150,  10)      # the letters' shadow, a darker green
WHITE  = (255, 255, 255)

S = 1024                      # master size; everything is in these units
R = int(S * 0.222)            # corner radius

LINES  = ("JS", "RF")
WIDTH  = 0.66                 # the letters' width, of the icon's
GAP    = 0.18                 # between the lines, of a line's height
SHIFT  = 0.016                # the shadow's offset, down and right
GLYPH  = 400                  # the size the letters are set at, before scaling
BOLDER = 14                   # the stroke added to the letters when small

PNG_SIZE  = 256
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
SMALL     = 32                # at this size and below, the bolder drawing


def letters(font, stroke):
    """The lines in white on clear, each centred, cropped to the letters."""
    boxes = [font.getbbox(t, stroke_width=stroke) for t in LINES]
    widths = [b[2] - b[0] for b in boxes]
    line_h = max(b[3] - b[1] for b in boxes)
    gap = int(GAP * line_h)
    mask = Image.new("L", (max(widths), line_h * len(LINES) + gap * (len(LINES) - 1)), 0)
    d = ImageDraw.Draw(mask)
    for i, (t, b, w) in enumerate(zip(LINES, boxes, widths)):
        d.text(((mask.width - w) / 2 - b[0], i * (line_h + gap) - b[1]), t, font=font,
               fill=255, stroke_width=stroke, stroke_fill=255)
    return mask


def draw_master(font, small):
    m = letters(font, BOLDER if small else 0)
    scale = WIDTH * S / m.width
    m = m.resize((int(m.width * scale), int(m.height * scale)), Image.LANCZOS)
    x, y = (S - m.width) // 2, (S - m.height) // 2

    img = Image.new("RGBA", (S, S), GREEN)
    if not small:
        off = int(S * SHIFT)
        img.paste(Image.new("RGBA", m.size, SHADOW), (x + off, y + off), m)
    img.paste(Image.new("RGBA", m.size, WHITE), (x, y), m)

    # keep the rounded silhouette clean
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=R, fill=255)
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    ImageDraw.Draw(out).rounded_rectangle([2, 2, S - 3, S - 3], radius=R,
                                          outline=(0, 0, 0, 70), width=4)
    return out


def icon(size, masters):
    return masters[size <= SMALL].resize((size, size), Image.LANCZOS)


def main(font_path, png, ico):
    font = ImageFont.truetype(font_path, GLYPH)
    masters = {False: draw_master(font, False), True: draw_master(font, True)}
    icon(PNG_SIZE, masters).save(png)
    print("wrote", png)
    # Each size is given as its own image: Pillow's ICO writer would
    # otherwise scale every size down from the largest.
    frames = [icon(s, masters) for s in ICO_SIZES]
    frames[-1].save(ico, sizes=[(s, s) for s in ICO_SIZES],
                    append_images=frames[:-1])
    print("wrote", ico)


if __name__ == "__main__":
    if len(sys.argv) < 2 or not sys.argv[1].lower().endswith((".ttf", ".otf")):
        sys.exit(__doc__)
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1],
         sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "jsrf-pc.png"),
         sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "..", "windows", "jsrf.ico"))
