"""JSRF-PC's button pictures (README.md beside this file), made from the pictures spriteice chose on 22 November 2026
from two packs, both CC0: Juliocacko's Free Input Prompts for the pads, and Xelu's Free Controller & Key Prompts for
the keys, the mouse, and the PlayStation's Create and Options.

    python3 make.py <chosen folder> <Xelu's Blank_Black_Normal.png> <folder to write the PNGs to>

The chosen folder holds XBOX, PLAYSTATION, NINTENDO and KEYBOARD, the packs' pictures by their own names (Enter_Key_Dark
as spriteice edited it, 100 x 79). Each picture here is one of them by the name a set asks for (../prompt_art.c): the
Xbox and PlayStation ones shaded the Nintendo ones' grey, the PlayStation D-pad's lit arms and left stick's arrows
outlined, and every one's colours more saturated (below), written again as 8-bit RGBA. Needs Python 3 and Pillow; the
same pictures give the same pixels."""
import colorsys
import math
import os
import sys
from PIL import Image

# Which picture each is: its folder and its name there.
X, P, N, K = 'XBOX', 'PLAYSTATION', 'NINTENDO', 'KEYBOARD'
PICTURES = {
    # Xbox: Juliocacko's, the face buttons in their colours.
    'xbox_a': (X, 'T_X_A_Color_Retro'), 'xbox_b': (X, 'T_X_B_Color_Retro'),
    'xbox_x': (X, 'T_X_X_Color_Retro'), 'xbox_y': (X, 'T_X_Y_Color_Retro'),
    'xbox_view': (X, 'T_X_Share'),              # two squares, the pack's name for it notwithstanding
    'xbox_menu': (X, 'T_X_X'),                  # three lines
    'xbox_share': (X, 'T_X_Share-1'),           # the Series pad's, an arrow out of a tray
    'xbox_ls': (X, 'T_X_Left_Stick_Click'), 'xbox_rs': (X, 'T_X_Right_Stick_Click'),
    'xbox_lb': (X, 'T_X_LB'), 'xbox_rb': (X, 'T_X_RB'), 'xbox_lt': (X, 'T_X_LT'), 'xbox_rt': (X, 'T_X_RT'),
    'xbox_dpad_up': (X, 'T_X_Dpad_Up'), 'xbox_dpad_down': (X, 'T_X_Dpad_Down'),
    'xbox_dpad_left': (X, 'T_X_Dpad_Left'), 'xbox_dpad_right': (X, 'T_X_Dpad_Right'),
    # The whole D-pad, its left and right lit, and its up and down; the left stick with every way's arrow, with
    # left and right's, and with up and down's (the graffiti editor's help lines, port/prompt_text.h).
    'xbox_dpad_all': (X, 'T_X_Dpad'), 'xbox_dpad_across': (X, 'T_X_Dpad_X'), 'xbox_dpad_updown': (X, 'T_X_Dpad_Y'),
    'xbox_lstick_all': (X, 'T_X_L_2D'), 'xbox_lstick_across': (X, 'T_X_L_X'),
    'xbox_lstick_updown': (X, 'T_X_L_Y'),
    # PlayStation: Juliocacko's, with Xelu's PS Vita Select and Start as Create and Options.
    'ps_cross': (P, 'T_P4_Cross_Color_Retro'), 'ps_circle': (P, 'T_P4_Circle_Color_Retro'),
    'ps_square': (P, 'T_P4_Square_Color_Retro'), 'ps_triangle': (P, 'T_P4_Triangle_Color_Retro'),
    'ps_create': (P, 'Vita_Select'), 'ps_options': (P, 'Vita_Start'),
    'ps_l3': (P, 'T_P4_Left_Stick_Click_Alt_Retro'), 'ps_r3': (P, 'T_P4_Right_Stick_Click_Alt_Retro'),
    'ps_l1': (P, 'T_P4_L1_Retro'), 'ps_r1': (P, 'T_P4_R1_Retro'),
    'ps_l2': (P, 'T_P4_L2_Retro'), 'ps_r2': (P, 'T_P4_R2_Retro'),
    'ps_dpad_up': (P, 'T_P4_Dpad_UP_Retro'), 'ps_dpad_down': (P, 'T_P4_Dpad_Down_Retro'),
    'ps_dpad_left': (P, 'T_P4_Dpad_Left_Retro'), 'ps_dpad_right': (P, 'T_P4_Dpad_Right_Retro'),
    'ps_dpad_all': (P, 'T_P4_Dpad_Retro'), 'ps_dpad_across': (P, 'T_P4_Dpad_X_Retro'),
    'ps_dpad_updown': (P, 'T_P4_Dpad_Y_Retro'),
    'ps_lstick_all': (P, 'T_P4_L_2D_Retro'), 'ps_lstick_across': (P, 'T_P4_L_X_Retro'),
    'ps_lstick_updown': (P, 'T_P4_L_Y_Retro'),
    'touchpad': (P, 'T_P4_Touch_Pad_Retro'),   # every set's
    # Nintendo: Juliocacko's. The sticks are its L and R, as it has no stick pressed.
    'nintendo_a': (N, 'T_S_A_Retro'), 'nintendo_b': (N, 'T_S_B_Retro'),
    'nintendo_x': (N, 'T_S_X_Retro'), 'nintendo_y': (N, 'T_S_Y_Retro'),
    'nintendo_minus': (N, 'T_S_Minus_Retro'), 'nintendo_plus': (N, 'T_S_Plus_Retro'),
    'nintendo_capture': (N, 'T_S_Square_Retro'),
    'nintendo_ls': (N, 'T_S_L_Retro'), 'nintendo_rs': (N, 'T_S_R_Retro'),
    'nintendo_l': (N, 'T_S_LB_Retro'), 'nintendo_r': (N, 'T_S_RB_Retro'),
    'nintendo_zl': (N, 'T_S_LT_Retro'), 'nintendo_zr': (N, 'T_S_RT_Retro'),
    'nintendo_dpad_up': (N, 'T_S_Dpad_Up_Retro'), 'nintendo_dpad_down': (N, 'T_S_Dpad_Down_Retro'),
    'nintendo_dpad_left': (N, 'T_S_Dpad_Left_Retro'), 'nintendo_dpad_right': (N, 'T_S_Dpad_Right_Retro'),
    'nintendo_dpad_all': (N, 'T_S_Dpad_Retro'), 'nintendo_dpad_across': (N, 'T_S_Dpad_X_Retro'),
    'nintendo_dpad_updown': (N, 'T_S_Dpad_Y_Retro'),
    'nintendo_lstick_all': (N, 'T_S_L_2D_Retro'), 'nintendo_lstick_across': (N, 'T_S_L_X_Retro'),
    'nintendo_lstick_updown': (N, 'T_S_L_Y_Retro'),
    # The mouse: Xelu's, the pressed button lit; none lit for its back and forward buttons.
    'mouse_left': (K, 'Mouse_Left_Key_Dark'), 'mouse_middle': (K, 'Mouse_Middle_Key_Dark'),
    'mouse_right': (K, 'Mouse_Right_Key_Dark'), 'mouse_none': (K, 'Mouse_Simple_Key_Dark'),
}
# The keys: Xelu's dark ones. The letters, the numbers and F1 to F12 by their own names.
for c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789':
    PICTURES['key_' + c.lower()] = (K, f'{c}_Key_Dark')
for i in range(1, 13):
    PICTURES[f'key_f{i}'] = (K, f'F{i}_Key_Dark')
for name, xelu in [
        ('enter', 'Enter'), ('esc', 'Esc'), ('backspace', 'Backspace'), ('tab', 'Tab'), ('space', 'Space'),
        ('minus', 'Minus'), ('plus', 'Plus'),           # the = and + key, and the keypad's +
        ('bracket_left', 'Bracket_Left'), ('bracket_right', 'Bracket_Right'),
        ('semicolon', 'Semicolon'), ('quote', 'Quote'), ('grave', 'Tilda'),
        ('comma', 'Mark_Left'), ('period', 'Mark_Right'),
        ('slash', 'Question'),                          # the / and ? key: Xelu's Slash is the backslash
        ('backslash', 'Slash'), ('asterisk', 'Asterisk'),
        ('up', 'Arrow_Up'), ('down', 'Arrow_Down'), ('left', 'Arrow_Left'), ('right', 'Arrow_Right'),
        ('ctrl', 'Ctrl'), ('shift', 'Shift'), ('alt', 'Alt'), ('win', 'Win'), ('caps_lock', 'Caps_Lock'),
        ('print_screen', 'Print_Screen'), ('insert', 'Insert'), ('delete', 'Del'), ('home', 'Home'),
        ('end', 'End'), ('page_up', 'Page_Up'), ('page_down', 'Page_Down'), ('num_lock', 'Num_Lock')]:
    PICTURES['key_' + name] = (K, xelu + '_Key_Dark')
# key_blank, which the engine writes a name on, is Xelu's blank dark key, the second argument.

# Shading (spriteice, 22 November 2026: "shade them the same grey as the nintendo buttons, excluding the xbox face
# buttons"): each Xbox and PlayStation picture's body grey, the mean of its opaque, unsaturated pixels darker than
# 160, is moved to the Nintendo pictures' by a straight lift toward white, v' = 255 - (255 - v) * (255 - TARGET) /
# (255 - body), so white letters stay white and the shading keeps its steps. Coloured pixels (the PlayStation's
# symbols) keep their colour: the lift fades out as a pixel's saturation goes from 20 to 60. Alpha as it is. One
# already that grey or lighter (the Vita's Select and Start, 70) is left as it is.
TARGET = 69
SHADED = {X, P}
UNSHADED = {'T_X_A_Color_Retro', 'T_X_B_Color_Retro', 'T_X_X_Color_Retro', 'T_X_Y_Color_Retro'}
# Outlining (spriteice: "the dpad up, down, left, right for playstation", with its sticks' arrows): a black outline
# OUTLINE pixels wide round the PlayStation D-pad's lit arms and its left stick's arrows, every light pixel (each
# channel over 180, alpha 64 or more): each pixel near it that isn't light is blended toward black by how far it lies
# inside the band, so its outer edge is smooth; one that is clear becomes black at that coverage.
OUTLINE = 2.0
OUTLINED = {'T_P4_Dpad_UP_Retro', 'T_P4_Dpad_Down_Retro', 'T_P4_Dpad_Left_Retro', 'T_P4_Dpad_Right_Retro',
            'T_P4_Dpad_X_Retro', 'T_P4_Dpad_Y_Retro', 'T_P4_L_2D_Retro', 'T_P4_L_X_Retro', 'T_P4_L_Y_Retro'}
# Saturating, last (spriteice, 22 November 2026: "increase colour saturation on the glyphs by 25%", then "turn up the
# saturation on the xbox buttons a little more (the red and green need to be very similar to the games glyphs" and
# "turn it up a bit more on the playstation buttons too. the nintendo ones are good"), each folder by its own way and
# strength. 'luminance': each colour moved further from its own grey, the luminance L = 0.2126 R + 0.7152 G + 0.0722 B,
# v' = L + k * (v - L), which keeps its brightness. 'hsv': its HSV saturation times k, at most 1, its hue and value
# kept, which deepens a light colour (the PlayStation's symbols, which the first barely moves). Rounded and held to
# 0..255; a grey pixel stays as it is either way, and so do black and white; alpha as it is.
SATURATION = {X: ('luminance', 1.75), P: ('hsv', 1.5), N: ('luminance', 1.25), K: ('luminance', 1.25)}


def pixels(im):
    return list(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())


def body(px):
    d = [(r + g + b) / 3 for r, g, b, a in px if a >= 200 and max(r, g, b) - min(r, g, b) <= 24
         and (r + g + b) / 3 < 160]
    return sum(d) / len(d) if d else None


def shade(im):
    px = pixels(im)
    s = body(px)
    if s is None or s >= TARGET:
        return im
    k = (255 - TARGET) / (255 - s)
    new = []
    for r, g, b, a in px:
        sat = max(r, g, b) - min(r, g, b)
        w = 1.0 if sat <= 20 else 0.0 if sat >= 60 else (60 - sat) / 40
        new.append(tuple(round(v + w * ((255 - (255 - v) * k) - v)) for v in (r, g, b)) + (a,))
    out = Image.new('RGBA', im.size)
    out.putdata(new)
    return out


def light(p):
    return p[3] >= 64 and min(p[:3]) > 180


def outline(im):
    im = im.copy()
    w, h = im.size
    px = im.load()
    mask = {(x, y) for y in range(h) for x in range(w) if light(px[x, y])}
    reach = int(math.ceil(OUTLINE)) + 1
    for y in range(h):
        for x in range(w):
            p = px[x, y]
            if (x, y) in mask or light(p):
                continue
            dist = min((math.hypot(x - mx, y - my) for mx in range(x - reach, x + reach + 1)
                        for my in range(y - reach, y + reach + 1) if (mx, my) in mask), default=None)
            if dist is None:
                continue
            cov = max(0.0, min(1.0, OUTLINE + 0.5 - dist))
            if cov <= 0:
                continue
            al = p[3] / 255
            na = cov + al * (1 - cov)
            rgb = tuple(round((p[i] * al * (1 - cov)) / na) for i in range(3)) if na > 0 else (0, 0, 0)
            px[x, y] = rgb + (round(na * 255),)
    return im


def saturate(im, how):
    way, k = how
    new = []
    for r, g, b, a in pixels(im):
        if way == 'hsv':
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            rgb = tuple(c * 255 for c in colorsys.hsv_to_rgb(h, min(1.0, s * k), v))
        else:
            lum = 0.2126 * r + 0.7152 * g + 0.0722 * b
            rgb = tuple(lum + k * (c - lum) for c in (r, g, b))
        new.append(tuple(max(0, min(255, round(c))) for c in rgb) + (a,))
    out = Image.new('RGBA', im.size)
    out.putdata(new)
    return out


def make(chosen, folder, name):
    im = Image.open(os.path.join(chosen, folder, name + '.png')).convert('RGBA')
    if folder in SHADED and name not in UNSHADED:
        im = shade(im)
    if name in OUTLINED:
        im = outline(im)
    return saturate(im, SATURATION[folder])


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    chosen, blank, out = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    for name, (folder, source) in sorted(PICTURES.items()):
        make(chosen, folder, source).save(os.path.join(out, name + '.png'), optimize=True)
    Image.open(blank).convert('RGBA').save(os.path.join(out, 'key_blank.png'), optimize=True)
    print(f'{len(PICTURES) + 1} pictures in {out}')


if __name__ == '__main__':
    main()
