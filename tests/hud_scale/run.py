"""HUD scale's maths (src/nv2a_gl/nv2a_hud_scale.c), as the engine builds it:
optimised, NDEBUG, with the warnings CI counts, driven by test_main.c. And,
by text, the shader's step it mirrors, on both of the vertex shader's paths
(src/nv2a_gl/nv2a_vsh.c), and Display's rows with the HUD submenu
among them (src/nv2a_gl/nv2a_gl.c): that the renderer then draws the HUD at
its size needs the game, the test build's HUD steps.
Requires GCC on PATH, including MinGW on Windows.
CC, when set, is the compiler instead of gcc.
"""
import os
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
cc = shlex.split(os.environ.get("CC", "gcc"))
# gl_Position.xy = gl_Position.xy * (1.0 + hudSize.x) - hudSize.yz * (hudSize.x * gl_Position.w);
# over two of the source's string lines
STEP = re.compile(r'gl_Position\.xy = gl_Position\.xy \* \(1\.0 \+ hudSize\.x\)\\n"\s*'
                  r'"\s*- hudSize\.yz \* \(hudSize\.x \* gl_Position\.w\);\\n"')

vsh = (root / "src/nv2a_gl/nv2a_vsh.c").read_text(encoding="utf-8")
steps = len(STEP.findall(vsh))
if steps != 2:
    sys.exit(f"hud_scale: the shader's step is in nv2a_vsh.c {steps} times, not twice")
if vsh.count("uniform vec3 hudSize;") != 1:
    sys.exit("hud_scale: nv2a_vsh.c doesn't declare hudSize once")

# Display's rows (nv2a_gl.c, sdl_menu_display), by text: Fullscreen, Render
# scale, Aspect ratio, then the HUD submenu, added in that order
# (spriteice: "Render scale up", HUD scaling "as it's own submenu under
# aspect ratio", and its name, "HUD"), with HUD scale and Disable HUD in it; Draw distance's rows
# go under the submenu (port/draw_distance.c). Each row as the menu's rules
# have a row (menu.h, docs/menu-format.md): a hint of one sentence of at
# most MENU_HINT_LONGEST characters; HUD scale a number from 0.5x to 1.5x a
# tenth a step, and both rows at the HUD's own (1x, shown) in each of File >
# Presets' three.
gl = (root / "src/nv2a_gl/nv2a_gl.c").read_text(encoding="utf-8")
start = gl.find("static void sdl_menu_display(void)\n{")
if start < 0:
    sys.exit("hud_scale: no sdl_menu_display in nv2a_gl.c")
display = gl[start:gl.index("\n}\n", start)]
group = re.search(r'#define SDL_HUD_GROUP "([^"]+)"', gl)
if not group or group.group(1) != "HUD":
    sys.exit(f"hud_scale: the submenu is {group and group.group(1)!r}")
draw = (root / "port/draw_distance.c").read_text(encoding="utf-8")
if f'#define DRAW_UNDER     "{group.group(1)}"' not in draw:
    sys.exit("hud_scale: Draw distance's rows don't go under HUD")
labels = re.findall(r'\.label = ("[^"]+"|\w+)', display)
if labels != ['"Fullscreen"', '"Render scale (Restart)"', '"Aspect ratio"', "SDL_HUD_GROUP",
              '"HUD scale"', '"Disable HUD"', "XBOX_GROUP_SKIPS", "XBOX_GROUP_ENHANCEMENTS"]:
    sys.exit(f"hud_scale: Display's rows are {labels}")
adds = re.findall(r"xbox_MenuAdd\((&?\w+)", display)
if adds != ["&fullscreen", "&scale", "rows", "groups"]:
    sys.exit(f"hud_scale: Display's rows are added as {adds}")
longest = int(re.search(r"#define MENU_HINT_LONGEST (\d+)",
                        (root / "src/menu/menu.h").read_text(encoding="utf-8")).group(1))
rows = {}
for label, end in (("SDL_HUD_GROUP", '"HUD scale"'), ('"HUD scale"', '"Disable HUD"'),
                   ('"Disable HUD"', "XBOX_GROUP_SKIPS")):
    row = display[display.rindex("{ .menu", 0, display.index(f".label = {label}")):
                  display.rindex("{ .menu", 0, display.index(f".label = {end}"))]
    hint = re.search(r'\.hint = "([^"]+)"', row)
    if not hint or len(hint.group(1)) > longest or not hint.group(1).endswith("."):
        sys.exit(f"hud_scale: {label}'s hint is {hint and hint.group(1)!r}")
    rows[label] = row
wants = {
    "SDL_HUD_GROUP": (".kind = XBOX_ITEM_GROUP",),
    '"HUD scale"': (".kind = XBOX_ITEM_NUMBER", ".group = SDL_HUD_GROUP",
                    ".min = NV2A_HUD_SCALE_MIN, .max = NV2A_HUD_SCALE_MAX, .step = NV2A_HUD_SCALE_STEP",
                    '.format = "%.1fx"', ".presets = XBOX_PRESETS_SET",
                    ".preset = { NV2A_HUD_SCALE_DEFAULT, NV2A_HUD_SCALE_DEFAULT, NV2A_HUD_SCALE_DEFAULT }"),
    '"Disable HUD"': (".kind = XBOX_ITEM_TOGGLE", ".group = SDL_HUD_GROUP",
                      ".presets = XBOX_PRESETS_SET, .preset = { 0, 0, 0 }"),
}
for label, want in wants.items():
    for w in want:
        if w not in rows[label]:
            sys.exit(f"hud_scale: {label}'s row has no {w}")
if ".last" in rows["SDL_HUD_GROUP"]:
    sys.exit("hud_scale: HUD is at Display's foot")
# kept as hudscale= and hudoff=, and read back from them
for w in ('sdl_cfg_put("hudscale", key)', 'nv_window_pref_num("hudscale", &v)',
          'sdl_cfg_set("hudoff", g_hud_off)', 'nv_window_pref_num("hudoff", &v)'):
    if w not in gl:
        sys.exit(f"hud_scale: no {w} in nv2a_gl.c")

with tempfile.TemporaryDirectory(prefix="hud_scale-") as directory:
    exe = str(pathlib.Path(directory) / "hud_scale.exe")
    subprocess.run(cc + ["-std=gnu11", "-O2", "-DNDEBUG", "-Wall", "-Wextra", "-Werror",
                         "-Isrc/nv2a_gl", "tests/hud_scale/test_main.c",
                         "src/nv2a_gl/nv2a_hud_scale.c", "-lm", "-o", exe],
                   cwd=root, check=True)
    try:
        run = subprocess.run([exe], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        sys.exit("hud_scale: no end in 60 s")
    text = run.stdout + run.stderr
    print(text, end="")
    if run.returncode != 0 or "hud_scale: all cases pass" not in text:
        sys.exit(f"hud_scale: exit {run.returncode}")
