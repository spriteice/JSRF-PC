# The button prompts' pictures

The pictures the button prompts draw (`../prompt_art.h`): a pad's buttons in
each style, and the keys and the mouse's buttons. spriteice chose them on 22
November 2026 from two packs, both dedicated to the public domain under CC0
1.0 Universal (`../../../LICENSES/CC0-1.0.txt`), which asks for nothing;
both authors are credited anyway, here and in NOTICE, with thanks:

- Juliocacko's Free Input Prompts (https://juliocacko.itch.io/free-input-prompts):
  every pad's buttons.
- Xelu's Free Controller & Key Prompts, by Nicolae "Xelu" Berbece of Those
  Awesome Guys (https://thoseawesomeguys.com/prompts/): the keys, the mouse,
  and the PS Vita's Select and Start.

Which picture is which:

- `xbox_*`: Juliocacko's Xbox pictures, the face buttons in their colours
  (A green, B red, X blue, Y yellow) with black letters; View two squares,
  Menu three lines, Share the Series pad's arrow out of a tray; the sticks
  pressed, and the bumpers and triggers lettered.
- `ps_*`: Juliocacko's PlayStation (Retro) pictures, grey buttons with the
  four symbols in colour; Create and Options are Xelu's PS Vita Select and
  Start, as Juliocacko's pack has neither. Neither pack has a Mute button:
  it is drawn on the blank key with its name (`../prompt_art.c`).
- `nintendo_*`: Juliocacko's Switch (Retro) pictures, the face buttons red,
  yellow, blue and green; Capture the square button. The pack has no stick
  pressed, so the sticks are its L and R sticks.
- `*_dpad_up`, `_down`, `_left` and `_right`: each pad's own D-pad with one
  arm lit.
- `*_dpad_all`, `_across` and `_updown`, and `*_lstick_all`, `_across` and
  `_updown`: Juliocacko's whole D-pad, its left and right arms lit, and its
  up and down; and his left stick with an arrow every way, left and right,
  and up and down. They stand for a D-pad or a left stick a line of the
  game's names by the ways it means (`../prompt_art.h`, the
  `PROMPT_GROUP_` pictures): the graffiti editor's help lines
  (`port/prompt_text.h`).
- `touchpad`: the PlayStation's, for every set.
- `key_*` and `mouse_*`: Xelu's dark keys, and his mouse with the pressed
  button lit (none lit for the back and forward buttons). The / and ? key
  is his `Question` key, as his `Slash` is the backslash; the keypad's /,
  *, -, + and Enter are the main keys' (* his `Asterisk`). `key_blank` is
  his blank dark key, which a key, or a pad's button, without a picture is
  drawn on, with its name written in white across its middle
  (`src/nv2a_gl/nv2a_prompts.c`, `LABEL_LEFT` and the rest): Scroll Lock,
  Pause, the keypad's full stop and the like, the PlayStation's Mute, and
  the paddles, P1 to P4 as the Xbox Elite labels them, as neither pack has
  a picture of them.

No single way of a stick, no right stick's ways, and no Home button, is
ever shown, so their pictures are left out.

## What is changed from the packs

spriteice, 22 November 2026, in the thread "Add button prompts for the
controller in use (part 14)":

- **The Xbox and PlayStation ones shaded:** "shade them the same grey as
  the nintendo buttons, excluding the xbox face buttons". Each picture's
  body grey (the mean of its opaque, unsaturated pixels darker than 160) is
  lifted to the Nintendo ones' (69) by a straight lift toward white, so its
  white letters stay white and its shading keeps its steps; the
  PlayStation's coloured symbols keep their colour. The Vita's Select and
  Start were already that grey.
- **The PlayStation D-pad outlined:** "As well as the dpad up, down, left,
  right for playstation" (with the sticks' arrows): its lit arms, and its
  left stick's arrows, given a black outline two pixels wide, smooth at its
  edge. The whole D-pad has no arm lit, so nothing to outline.
- **Enter a single bar:** Xelu's Enter is the L-shaped key; spriteice cut
  its step off, so it is a bar like Shift and Backspace, 100 x 79.
- **Every one's colours more saturated,** last: "increase colour
  saturation on the glyphs by 25%", then "turn up the saturation on the
  xbox buttons a little more (the red and green need to be very similar to
  the games glyphs" and "turn it up a bit more on the playstation buttons
  too. the nintendo ones are good". The Nintendo ones, the keys and the
  mouse have each pixel's colour moved a quarter further from its own grey
  (its luminance), the Xbox ones three quarters further; the PlayStation's
  symbols, light colours this barely moves, have their HSV saturation made
  1.5 times as much, which deepens them. The grey buttons, keys and mouse
  have no colour, so stay as they were.

Every other pixel is as the packs have it. The pads' pictures are 128
pixels square and the keys' 100 (Enter 100 x 79), the margin round each
as the pack gives it; the engine fits a picture to the size the game draws
it at, keeping its shape.

Every button but the face buttons is dark grey with white letters, so each
shows on the message box's white as on a dark scene; tests/prompts
(colours) checks the colours, the shading, the outline and how much of a
picture is white.

Xbox, PlayStation and Nintendo, the consoles' buttons and the PlayStation's
four symbols are their owners' trademarks, shown only to say which pad a
picture is for.

## Smoothed, and drawn larger

Graphics > Upscaling > Buttons (Off, 2x, 4x or 8x; Off by default,
spriteice, 2026-11-22) draws these pictures from smoothed copies: the
graphics card enlarges the whole picture with Super-xBR, at Upscaling's
Sharpness and Edge strength, and the copy is drawn in the picture's place,
the same shape (`src/nv2a_gl/nv2a_gl.c`, `prompts_copy_make`, and
`src/nv2a_gl/nv2a_prompts.c`, `nv2a_prompts_copy`). Off draws them as they
are here. tests/prompts (copy) checks that every picture is small enough
for a copy of the whole of it.

The pictures are drawn an eighth larger than the size each place first
had (spriteice, 2026-11-22: "10-15% larger everywhere, except the VS
menu"; `NV2A_PROMPT_LARGER` in `src/nv2a_gl/nv2a_prompts.h`). The VS
menu's are that size too for now: the size asked for them (spriteice,
2026-11-22: "make VS mode 15% smaller") waits for what draws that menu's
Back to be found (b3.1 row 9).

## Making them again

`make.py` makes every picture here from the pictures spriteice chose, by the
packs' own names in four folders (XBOX, PLAYSTATION, NINTENDO, KEYBOARD;
Enter as spriteice cut it), and Xelu's `Blanks/Blank_Black_Normal.png`:

    python3 make.py <the chosen folder> <Blank_Black_Normal.png> <folder>

It needs Python 3 with Pillow, and takes a few seconds. The same pictures
give the same pixels. Which picture each is, and every change above, is in
`make.py`.

`embed.cmake` builds the pictures into the engine (`../CMakeLists.txt`), so
the packages carry no new files but the CC0 text. A picture added here must
be one a set asks for (`../prompt_art.c`): tests/prompts checks both ways.
