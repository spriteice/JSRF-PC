JSRF-PC for Windows (x86_64)
============================

Jet Set Radio Future, statically recompiled and running natively.
https://github.com/spriteice/JSRF-PC

You need your own copy of the game -- the standalone US disc (not the Sega GT
2002 + JSRF combo disc, whose JSRF is a different build). This package is the
engine only: no game files, no textures, no music, no video.

The Windows build is the same engine as the Linux build, tested on
Windows 11. Bug reports are very welcome.


What you need
-------------
- Windows 10 or 11, x86_64.
- Graphics drivers with OpenGL 3.3 (any GPU from the last ten years, with its
  maker's driver -- the one Windows Update installs is usually fine).
- Your game files: the folder with default.xbe, Media\, and the rest in it,
  extracted from your own disc -- or a disc image of it (an XISO or a full
  disc image, usually .iso), which the launcher unpacks once.


Playing
-------
1. Unzip this archive anywhere. Keep the engine folder beside the .exe.
2. Run "JSRF-PC.exe".
3. The first time, it asks where the game is: pick the folder that contains
   default.xbe -- or the folder your .iso is in, and it offers to unpack it
   (once, into %LOCALAPPDATA%\JSRF-PC). It remembers the choice.

Windows may warn that the app is from an unknown publisher (it is not signed:
that costs money every year). Click "More info", then "Run anyway".

You can also drop a game folder or a .iso onto "JSRF-PC.exe", or use
it from a command prompt:

  "JSRF-PC.exe" --resolution R  the height the game is drawn at: default (the
                                Xbox's 640x480), 720, 900, 1080, 1440, 1600,
                                2160, or match (your screen's, the default);
                                F9 in game has the same choices; remembered
  "JSRF-PC.exe" --scale N       as before b2.2, 1 to 6: the resolution at
                                least that high, up to 2160
  "JSRF-PC.exe" --fullscreen    always starts full screen
  "JSRF-PC.exe" --minimized     starts minimized, in a window, leaving the
                                keyboard where it is, for that start only
  "JSRF-PC.exe" --choose        asks where the game is again
  "JSRF-PC.exe" --version       which version this is
  "JSRF-PC.exe" --play DIR      plays a recording (Developer > Playback) with
                                nobody at the controller, and closes at its
                                end


Controls
--------
Controller: any pad SDL knows -- Xbox, PlayStation, Switch Pro, and many more.

Keyboard and mouse (keys by position, so W A S D on QWERTY is Z Q S D on
AZERTY). These are the defaults: Input > Keyboard layout in the in-game menu
changes them (see Layouts and button prompts below).

  W A S D       move (left stick), menus    Arrow keys   camera (right stick)
  Space         A -- jump                   Left Shift   B -- boost
  C             X                           V            Y
  E             R trigger -- talk, spray    Q            L trigger
  R             Black                       T            White
  Enter         Start                       Esc          Back
  Left Ctrl     left stick click            Right Ctrl   right stick click
  U H J K       the d-pad's up, left, down, and right, for the menus
  Mouse         the left button is A too, the middle button B, and the right
                button the R trigger; moving it turns the free camera while
                that is on (see Free camera below)

  F1                 the in-game menu (also Back on a controller, which is
                     then the menu's alone): File, Display, Graphics, Audio,
                     Input, Gameplay, and Developer
  F11 or Alt+Enter   full screen or a window, full screen by default;
                     remembered
  F10                4:3, or widescreen at your screen's own shape, the
                     default (Display > Aspect ratio has 16:10, 16:9 and 21:9
                     too): a wider view, not a stretch; the HUD keeps its
                     shape and sits at the screen's edges, the pause menus and
                     the race previews are laid out across the screen, the
                     graffiti editor's bar of static is stretched across with
                     its help line scrolling its whole length, the garage's
                     menus and VS mode's setup screen carry their colours on
                     to its edges, the result screens' banner, time and
                     choices sit at the right edge, the title fills the
                     screen, its opening's bars sweeping in from its edges;
                     remembered
  F9                 resolution, in a box of its own over the game: the height
                     the game is drawn at, Default (the Xbox's 640x480), 720p,
                     900p, 1080p, 1440p, 1600p, 2160p, or Match screen, your
                     screen's own height (up to 2160), the default. The width
                     follows the aspect ratio (1080p at 16:9 is 1920x1080). It
                     changes straight away, with no restart, and is remembered
                     as resolution= in the settings file. Display > Resolution
                     has the same choices.
  F8                 free camera: the right stick and the mouse turn the
                     camera around the player; remembered (see Free camera
                     below). R3 on a controller does the same

File > Presets in the in-game menu sets many settings at once. Xbox (Restart)
and Modern (Restart) change gameplay as well as visuals; Performance,
Balanced, and Quality change only visuals. Xbox is the game as the Xbox runs
it: 4:3 at Default resolution (640x480), with everything JSRF-PC adds to how
it looks and plays turned off, and Audio effects on, as the Xbox's own sound
has them. Modern is the author's preferred way to play: the HUD at 0.8x, the
draw distance, Far buildings and Far shadows at 3x, MSAA at 4x, anisotropic
filtering at 16x, Upscaling's Menus and HUD and Textures at 8x, Graffiti at 4x
and Buttons off, Sharp, Strong and both Cleanup ticks, Audio effects on,
Keyboard moves on Camera, Stick shape Square, Button prompts' Style Auto,
Default cam fix and Half pipe cam on and the game's other two camera shots
off, the free camera on, and Skip intro, Skip cutscenes, and Visual indicator
on. Xbox and Modern each ask first, and restart the game; the other three
apply straight away, with no restart.

Performance, which a new install starts with, has the draw distance, Far
buildings and Far shadows at 1x, no anti-aliasing or anisotropic filtering,
and Upscaling's Textures and Graffiti off. Balanced has the draw distance at
2x, Far buildings and Far shadows at 3x, MSAA at 4x in place of SMAA,
anisotropic filtering at 8x, and Upscaling at 4x but for Buttons. Quality has
the draw distance, Far buildings and Far shadows, anisotropic filtering,
Upscaling's Menus and HUD, Buttons, Textures and Graffiti, and both
anti-aliasings at their most, as far as the graphics card goes: it asks a lot
of the computer, and Far buildings and Far shadows at All draw parts of a map
you never see from where you are, such as the top of Dogenzaka Hill in its
sky.

All but Xbox have the resolution and aspect ratio at Match screen, Music info
in pause and See through fix on, and Upscaling's Menus and HUD at 4x or more.
A preset sets every setting but Developer's, Input's two layouts, the free
camera's own settings, Mute, Shader cache and, but for Xbox and Modern, Audio
effects: each one also puts the game full screen and the HUD shown, at its own
size but in Modern. Input, Gameplay, and Upscaling's Sharpness, Edge strength
and Cleanup are yours: only Xbox and Modern change them. The one ticked is the
one your settings match, if any. Below them, Reset (Restart) puts every
setting back to how a new install has it, once you say yes, and restarts the
game: Developer's switches and both layouts too. It keeps where the game is,
your saves, the shader cache, your logs, and Developer's recordings.

Display > HUD in the in-game menu has HUD scale, which draws the HUD at
0.5x to 1.5x its size (1x, the default), a tenth at each - or +, and
Disable HUD, which hides it. Each piece keeps its distance from the screen's
edges and grows or shrinks from there; the signs and the counter in the
middle of the screen grow or shrink in place. Both change straight away, and
are remembered as hudscale= (0.5 to 1.5) and hudoff= (1 for hidden) in the
settings file. In split screen the HUD keeps its own size, though Disable HUD
still hides it.

Display > Draw distance in the in-game menu is how far away the game draws the
things in its world: crowds, spray cans, props you can knock over, traffic,
the police and the race's checkpoints. 1x, the default, is the game's own
distances, 2x to 4x twice to four times them, and Unlimited as far as the
camera sees. It changes straight away, asks more of the computer the further
it goes, and is remembered as drawdistance= in the settings file (0 for 1x to
4 for Unlimited). Above 2x it will struggle on weak hardware.

Display > Building distance > Far buildings in the in-game menu draws more of
the map, with the things in it, so buildings further off don't pop in as you
come near. The game splits each map into areas, and draws only the ones the
area the camera is in lists. Off is the game's own; 1x (the default), 2x and
3x draw what the game would show from the areas one, two or three areas ahead;
and All draws every area the game ever shows, which can put a part of the map
in view where the game never shows it, such as the top of Dogenzaka Hill in
the sky at its bottom. At any choice, a few areas of Dogenzaka Hill, Hikage
Street, Kibogaoka Hill and Chuo Street aren't drawn from the places where they
would hang in the sky or stand in the street's view, and come in as you reach
them. In 99th Street's opening scene, the first time you come in, nothing more
than the game's own is drawn. It changes straight away, asks more of the
computer the further it goes, and is remembered as farbuildingsahead= in the
settings file (0 for Off to 4 for All).

Display > Building distance > Far shadows, below Far buildings, draws the
shadows of the areas Far buildings adds too, as many areas ahead as its own
choice, Off to All, and never further than Far buildings, so they don't pop in
as you come near, at the cost of minor visual glitches, such as patches of
light in shadows that move with the camera. Off, those areas have no shadows
until you're near enough for the game to show them itself. 1x by default. It
does nothing with Far buildings off, changes straight away, and is remembered
as farshadowsahead= in the settings file (0 for Off to 4 for All). Before
b2.1-hotfix the two were on or off, remembered as farbuildings= and
farshadows=: until you choose again, 1 there reads as 3x and 0 as Off.

Gameplay > Skips > Skip intro in the in-game menu leaves out the logos and the
graffiti disclaimer the game opens with: it starts at the title screen's
loading. Off by default, so the intro plays as on the Xbox. It is remembered
as skipintro= in the settings file (0 or 1).

Gameplay > Skips > Skip cutscenes in the in-game menu lets you leave a story
scene, or a preview where the camera follows a rival to their place: while one
plays, press A, and "Press A again to skip" shows at the bottom right for 3
seconds; press A again while it shows, and the scene ends as it does at its
own end, the story going on from there. With it on, that is the one way out of
every scene and preview, those you've seen before too. Off by default, so
scenes play to their end as on the Xbox. It changes straight away, and is
remembered as skipcutscenes= in the settings file (0 or 1).

Gameplay > Graffiti souls > Visual indicator in the in-game menu shows the
graffiti soul, in white, at the bottom left of the HUD while one of an area's
special soul challenges is under way and on track: a course of moves, or
Shibuya Terminal's 13 platforms. It turns red and goes when the challenge
fails, and goes when the soul is yours. The challenges themselves are as on
the Xbox. Off by default. It changes straight away, and is remembered as
soulindicator= in the settings file (0 or 1).

Display > Enhancements > Music info in pause in the in-game menu shows the
song playing, its number, title and artist, in both pause menus, on the left,
above Rotate map. It follows the songs of the street mixes too, and shows
nothing for the title's and the story's own music. On by default. It changes
straight away, and is remembered as musicinpause= in the settings file
(0 or 1).

Display > Enhancements > See through fix in the in-game menu shows characters
and dynamic world objects through glass and water, tinted by them: the game
draws them after the glass, and on the Xbox the glass hides them, as the crowd
behind a shop's window. Faded characters, as your skater close to the camera,
and the level's arrow as it fades in, let what is behind them show through
too, tinted by them, their own far side as well: on the Xbox they hide what
the game draws after them, such as other characters, glass and water. Their
dark outline then shows round the edge of each part, not across it. The copies
of your skater that trail behind at speed are drawn as on the Xbox: in shadow
they are shadowed, and they hide the dash's burst behind them. So are the
other moving models without a dark outline, such as the crowd, which still
shows through glass. On by default. It changes straight away, and is
remembered as seethru= in the settings file (1 or 0).

Graphics > Anti-aliasing (MSAA) in the in-game menu smooths jagged edges:
Off (the default), 2x, 4x, or 8x, as far as the graphics card goes. It changes
straight away, with no restart, and is remembered as msaa= in the settings
file (0, 2, 4, or 8). More asks more of the graphics card, the more so at the
higher resolutions.

Graphics > Anti-aliasing (SMAA) in the in-game menu smooths jagged edges with
a quick pass over each finished frame, at much the same small cost whatever is
on screen. It smooths the HUD too. It works alone or on top of MSAA. Off by
default; it changes straight away, with no restart, and is remembered as smaa=
in the settings file (0 or 1).

Graphics > Anisotropic filtering in the in-game menu keeps textures sharp
where they are seen at a steep angle, such as streets and walls running into
the distance: Off (the default), 2x, 4x, 8x, or 16x, as far as the graphics
card goes. It changes straight away, with no restart, though the textures it
reaches take a moment to load again, and is remembered as aniso= in the
settings file (0, 2, 4, 8, or 16). It leaves things that face the screen, such
as the HUD, as they were.

Graphics > Upscaling > Menus and HUD in the in-game menu draws the menus,
text, HUD and icons from sharper copies of the game's art: Off, 2x, 4x (the
default), or 8x. The graphics card makes each copy the first time that art is
shown, a few a frame. It helps above Default resolution; at Default it makes
nothing, as the art is drawn at its own size there. It changes straight away,
with no restart, and is remembered as upscale_2d= in the settings file (0, 2,
4, or 8). Each size suits a resolution: 2x for 720p and 900p, 4x for 1080p to
1600p, and 8x for 2160p, whose copies take the most memory.

Graphics > Upscaling > Buttons in the in-game menu draws the button prompts'
pictures (Input > Button prompts, below) from copies enlarged as the game's
art is: Off (the default), 2x, 4x, or 8x. The pictures are smooth and at most
128 pixels a side, larger than the game draws them up to about 1440p, so Off
draws them as they are; 2x is enough at 2160p. It changes straight away, with
no restart, and is remembered as upscale_buttons= in the settings file (0, 2,
4, or 8).

Graphics > Upscaling > Textures in the in-game menu draws the textures of the
3D world (walls, the ground, signs and the characters) from sharper copies:
Off (the default), 2x, 4x, or 8x. The graphics card makes the copies in a
level's first moments, a few a frame. They show where a surface comes close
enough to the camera for its texture to be enlarged, at any resolution and
more so the higher it is. Distant surfaces, cut-out edges such as fences, and
the characters' light and shadow look as they always have. It changes straight
away, with no restart, and is remembered as upscale_3d= in the settings file
(0, 2, 4, or 8). No copy is larger than 2048 a side, so 8x enlarges the larger
textures 4x. The copies take up to 4 GB of the graphics card's memory, 1 to
2.5 GB at 4x in play; whatever doesn't fit keeps the game's own textures.

Graphics > Upscaling > Graffiti in the in-game menu draws the graffiti tags
from sharper copies: Off (the default), 2x, 4x, or 8x. The tags come 16 to a
sheet of 1024 x 1024, and the graphics card makes a sheet's copy the first
time it is drawn, after the 3D world's; the garage's graffiti picker has its
copy made as it draws it, which can pause the menu for a moment. It changes
straight away, with no restart, and is remembered as upscale_graffiti= in the
settings file (0, 2, 4, or 8). The copies take up to 1 GB of the graphics
card's memory, apart from Textures': about 85 MB a sheet at 4x and 340 MB at
8x.

Graphics > Upscaling > Sharpness in the in-game menu sets how crisp the edges
in the 2D, 3D, buttons' and graffiti copies are: Soft, Normal (the default),
or Sharp. Normal is Super-xBR as its author set it; Sharp draws edges crisper
and Soft softer, neither with halos. It changes straight away, with no
restart, making every copy again, which can pause the game for a moment, and
is remembered as upscale_sharpness= in the settings file (0, 1, or 2).

Graphics > Upscaling > Edge strength in the in-game menu sets how faint a
diagonal edge the 2D, 3D, buttons' and graffiti copies follow: Soft, Normal
(the default), or Strong. Normal is Super-xBR as its author set it; Strong
follows fainter diagonals too, for smoother lines in fine detail, and Soft
only clear ones, blending the rest. It changes straight away, with no restart,
making every copy again, which can pause the game for a moment, and is
remembered as upscale_edge_strength= in the settings file (0, 1, or 2).

Graphics > Upscaling > Cleanup in the in-game menu cleans the game's textures
up before the copies are made, with two ticks, each off by default: DXT
deblock smooths the faint steps where a DXT texture's 4x4 blocks meet, and
De-dither smooths the dot patterns of 16-bit and DXT textures. Each leaves
larger differences, edges in the picture, as they are, and neither changes a
texture's see-through parts. A change takes effect straight away, with no
restart, making every copy again, which can pause the game for a moment. File
> Presets > Xbox turns both off, and Modern both on. They are remembered as
upscale_deblock= and upscale_dedither= in the settings file (0 or 1).

Graphics > Shader cache in the in-game menu (on by default) keeps the shaders
the game builds, in the cache folder. Each time the game starts, they are all
built again before it begins, behind a progress bar, so none are built while
you play. It is remembered as shaders= in the settings file (1 or 0). Changing
it in the menu restarts the game, after asking. No preset changes it.

Audio > Mute in the in-game menu silences the game's sound straight away, and
ticked again, brings it back; the game and its sound run on as ever, only
silent. Off by default. No preset changes it, and it is remembered as mute= in
the settings file (1 or 0).

Audio > Audio effects in the in-game menu (on by default) adds our own
versions of the console's sound effects: the 3D sounds placed to your left or
right as the game sets them, the characters' voices kept on them as they move,
the reverb the game sets for a place, as in the sewers, and a limiter of their
own for the 3D sounds, so a busy moment no longer turns the music and voices
down. Turned off, the sound is as before them. It is remembered as
audioeffects= in the settings file (1 or 0). Changing it in the menu restarts
the game, after asking. Only File > Presets > Xbox and Modern change it,
turning it on.

Developer > Log to file in the in-game menu (on by default) writes the game's
output to last-run.log. Turned off, nothing more is written to it until it is
turned on again, and what is there already stays. It changes straight away and
is remembered as logging= in the settings file (1 or 0). Developer > Logging
adds the engine's own diagnostic lines to the log, for bug reports,
Developer > Engine switches turns parts of the engine off or changes how they
work, to compare, Developer > Performance has GPU queue, Sleep while waiting
for the GPU, and Compressed textures direct, each on by default and ticked, to
untick and compare, and Developer > Experimental has changes to how the game
runs, to try out, each off by default: Random graffiti, which sprays designs
drawn at random from the ones your save has unlocked, a different one at each
spot, in place of the five chosen in the garage, and Sun flare fix, which
hides the sun's glare behind a wall or slope where the game's own check misses
it. Each row stands for an environment variable, kept in developer.cfg beside
the settings file as NAME=1 or NAME=0, and applies from the next start: File >
Restart restarts the game, after asking.
Developer > Purge shaders asks first, then restarts the game and deletes the
shader cache as it starts: each shader is built again when the game first
needs it.

Developer > Missions restarts the game at a story mission, for testing, as any
of the game's characters. It is a developer option, so a mission started this
way may not play as it does in the story. A run started there keeps its saves
apart from your own, in mission-saves in the save folder.

Developer > Playback > Record (Restart) asks first, then restarts the game and
records every press the game reads, and the free camera's stick, on and off
and settings, by the game's own frame, until Developer > Playback > Stop
recording. Each recording is a folder in recordings beside the settings file
(portable\recordings in portable mode), named by the time it started (UTC).
Play (Restart) asks first, then restarts the game and plays a recording back
with nobody at the controller, with the recording's free camera settings in
place of yours until it ends; any press on a controller or the keyboard, or a
change to the free camera, ends it and gives the game back. A recording is for
testing: it plays back only as far as the game keeps to the recorded route,
and last-run.log says how far that was.


Free camera
-----------
F8, R3 on a controller, or freecam=1 in the settings file, gives the right
stick to the camera: it turns around the player, and forward is wherever it
looks. The mouse turns it too, as the right stick does: while the free camera
is on and the in-game menu is shut, the game holds the pointer, hidden and
kept in the window, until the menu opens, the free camera is turned off, or
you switch to another window. The L trigger puts it back behind the player, as
it does the game's camera. Talking to someone doesn't turn it to them. The
game's own shots, as when you spray graffiti, are Camera's (below); after one,
the free camera glides back to where you had it, the right stick or the mouse
taking over at once. It steers only the camera of the skater you control: when
the game moves a skater by itself, as when a scene follows another skater, the
game's own camera shows it. Pausing keeps the view. F8 and R3 are remembered.
The settings file (see "Where things are kept") also takes these, one per
line, with no spaces around "=". The values shown are the defaults, which a
key that is left out, or out of range, gets:

  freecam=0            1: on
  invert_x=0           1: right turns the view left, with the stick and the
                       mouse
  invert_y=0           1: up tilts the view down, with the stick and the mouse
  sensitivity_x=5      left-right speed, 1 to 10: 36 degrees a second per step
  sensitivity_y=5      up-down speed, 1 to 10
  deadzone=0.1         how far the stick moves before the camera does, 0 to 1
                       (0.1 is 10%)
  mouse=1              0: the mouse leaves the camera and the pointer alone
  mouse_sensitivity=5  the mouse's speed, 1 to 10: 0.02 degrees a count per
                       step
  autocentre=0         auto centre: once the right stick and the mouse are let
                       go, the view eases back behind the player. 0 off, 1
                       mild (after 2 seconds, slowly), 2 medium, 3 high (after
                       a second), 4 aggressive (after half a second, quickly)

Input > Free camera in the in-game menu has all of these too, and keeps them
here as F8 does; its Reset to default puts all of them but freecam= back to
these values. The game does not use R3, and Toggle free camera in Input >
Controller layout can move the switch to another button.

Input > Behaviour > Camera in the in-game menu is five ticks. Default cam fix
keeps the game's own camera from showing through a wall beside it, which
happens on the Xbox too and more in widescreen: near a wall, the view shifts a
little to the side, or in. Off by default, so the game's camera is as on the
Xbox. No skater fade keeps the camera as far from your skater as the game
shows them whole, seeing through a wall nearer than that; off by default, so
the camera comes in to the wall and your skater fades out as it nears them.
Then three of the game's own camera shots: Graffiti cam (the camera turning to
the graffiti you spray), Half pipe cam (a jump in a half pipe), and Loop cam
(the fixed shots a stage places, as on a loop). Ticked, the game takes the
camera for them as on the Xbox, with the free camera on or off; unticked, they
don't play and the camera keeps following you. With Half pipe cam unticked,
the camera rises and falls with you through a half pipe's jump, the free
camera too, and the free camera can go through a half pipe's wall, riding it
with the camera turned into it or coming down from a big jump: a known issue,
which is why Modern leaves it ticked. The camera at a level's exit is always
as on the Xbox. All three are ticked by default. Each tick changes straight
away, and is remembered as dcamfix=, noskaterfade=, graffiti_cam=,
half_pipe_cam= and loop_cam= in the settings file (0 or 1).

Input > Behaviour > Keyboard moves in the in-game menu sets how W A S D move
the skater: from the way the camera faces (Camera, the default), or from the
way the skater faces (Skater), where W goes the way the skater faces, A and D
turn it while held, and S turns it round. It changes straight away, and is
remembered as keyboard_moves= in the settings file (0 Camera, 1 Skater).

Input > Behaviour > Stick shape in the in-game menu sets how far a
controller's sticks reach on a diagonal: Square (the default), each stick
stretched onto the square the Xbox controller's sticks reach, so a part push
on a diagonal counts up to 41% more, as on the Xbox, or Round, the sticks as
the pad gives them. It changes straight away, and is remembered as
stick_shape= in the settings file (0 Square, 1 Round).


Layouts and button prompts
--------------------------
Input > Controller layout and Input > Keyboard layout in the in-game menu
change which button of the pad, or which keys or mouse buttons, press each of
the game's controls: choose a control, then press its button or key. The
control that had it gets the one it replaces, so the two swap. A control on
the keyboard takes two keys, and F1, or Back on a controller, leaves one with
none. Any key can be used but F1 and F8 to F11, which stay the settings keys,
and any button of the pad but Guide and Back. Controller layout's last row,
Toggle free camera, is the free camera's switch, R3 by default. Each change is
kept in remap.cfg beside the settings file, Reset to default puts a page back,
and no preset changes them.

Input > Button prompts > Style in the in-game menu sets the look of the game's
button icons: Auto, Default (the default), Xbox, PlayStation, Nintendo, or
Keyboard. Default is the game as the Xbox shows it. Auto follows the pad you
last pressed, and shows keys while the keyboard or the mouse was the last
thing you used; the others fix the look. In every style but Default, the icons
follow both layouts, and the A and B in the game's text and beside its message
boxes, the buttons, triggers and left thumbstick that the talks and the
missions' messages name (the tutorial's among them, each picture coming in as
a talk types it out), the controls in the help line along the top of the
graffiti editor, and the Start at a graffiti save point are the style's
pictures. It changes straight away, and is remembered as button_prompts= in
the settings file (0 Auto, 1 Default, 2 Xbox, 3 PlayStation, 4 Nintendo, 5
Keyboard). The pads' pictures are Juliocacko's Free Input Prompts, and the
keys' and the mouse's Xelu's Free Controller & Key Prompts, both free to use
(CC0).


Where things are kept
---------------------
  Saves     in the game folder, under UDATA (where an Xbox keeps them), so
            the game folder has to be one you can write to. Copy UDATA
            somewhere to back your progress up.
  Settings  %LOCALAPPDATA%\JSRF-PC\settings
  Cache     %LOCALAPPDATA%\JSRF-PC\save
  Logs      %LOCALAPPDATA%\JSRF-PC\last-run.log (and logs\)

If the game stops unexpectedly, the launcher says so and offers to show the
log. Found a bug? Open an issue on GitHub with last-run.log and what you were
doing.


Portable mode
-------------
Make a folder named "portable" next to "JSRF-PC.exe", and everything
is kept in it instead -- nothing in %LOCALAPPDATA%, nothing in the game folder:

  portable\settings.cfg   settings
  portable\saves\         saves (UDATA and TDATA): back this folder up
  portable\logs\          last-run.log and the logs before it
  portable\cache\         the game's cache: safe to delete
  portable\games\         disc images unpacked in portable mode

The first time, it asks where the game is again, and copies the saves it
finds in the game folder into portable\saves (the originals stay). A game
folder inside this folder is remembered relative to it, so the whole folder
can be moved, to a USB stick for instance, even under another drive letter.

To update, extract the new version over this folder, or move portable\ into
the new one: deleting the folder deletes your saves with it. Graphics drivers
and the folder picker keep their own small files and settings in your user
profile as they do for any program.


On Linux, through Wine or CrossOver
-----------------------------------
The native Linux build is the better choice. The Windows one should run
under Wine too: in CrossOver, make a Windows 10 64-bit bottle and use
"Run Command..." on JSRF-PC.exe.


Legal
-----
Jet Set Radio Future is (c) SEGA. This is an unofficial fan project, not
affiliated with or endorsed by SEGA. This package contains the engine -- the
runtime and the game's code, machine-translated to native code -- and no game
data.

Licences: GPL version 2 or later for this project's own code, with an
additional permission for the game's own code, which isn't under it; MIT for
the code from JSRF Unleashed and xboxrecomp; LGPL-2.1-or-later for the MCPX
audio-processor code extracted from xemu; the bundled SDL2 library is under
the zlib licence, libepoxy under the MIT licence. See the licences folder.
Source code: https://github.com/spriteice/JSRF-PC, a fork of JuMatt's
JSRF Unleashed: https://github.com/JuMatt/JetSetRadioFuture-Unleashed
