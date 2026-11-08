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

  "JSRF-PC.exe" --scale N     internal resolution, 1 to 6 (default 2; F9
                              in game does the same); remembered
  "JSRF-PC.exe" --fullscreen  always starts full screen
  "JSRF-PC.exe" --choose      asks where the game is again
  "JSRF-PC.exe" --version     which version this is


Controls
--------
Controller: any pad SDL knows -- Xbox, PlayStation, Switch Pro, and many more.

Keyboard (by key position, so W A S D on QWERTY is Z Q S D on AZERTY):

  W A S D      move (left stick), menus     Arrow keys  camera (right stick)
  Space or J   A -- jump                    K           B
  H            X                            L           Y
  E            R trigger -- talk, spray     Q           L trigger
  R            Black                        F           White
  Enter        Start                        Esc         Back
  C / V        left / right stick click

  F1                 the in-game menu (also Back on a controller, which is
                     then the menu's alone): File, Display, Graphics,
                     Audio, Controller, and Developer
  F11 or Alt+Enter   full screen; remembered
  F10                4:3, or widescreen at your screen's own shape (Display
                     > Aspect ratio has 16:10, 16:9 and 21:9 too): a wider
                     view, not a stretch; the HUD keeps its shape and sits
                     at the screen's edges, the pause menus are laid out
                     across the screen, the title screen stays 4:3;
                     remembered
  F9                 resolution: the game is drawn at 1x to 6x the Xbox's
                     640x480 (2x by default), up to 3840x2880, and as much
                     wider as the shape in widescreen (5120x2880 at 16:9);
                     remembered. The game restarts to apply it, so save
                     first.
  F8                 free camera: the right stick turns the camera around
                     the player; remembered (see Free camera below). R3 on
                     a controller does the same

Display > Draw distance in the in-game menu is how far away the game draws
the things in its world: crowds, spray cans, props you can knock over,
traffic, the police and the race's checkpoints. 1x (the default) is the
game's own distances, 2x to 4x twice to four times them, and Unlimited as far
as the camera sees. It changes straight away, asks more of the computer the
further it goes, and is remembered as drawdistance= in the settings file (0
for 1x to 4 for Unlimited). Above 2x it will struggle on weak hardware.

Display > Far buildings in the in-game menu draws every part of the map the
game ever shows, with the things in it, not only the part it shows from where
the camera is. A part it never shows stays hidden, such as the second copy of
Dogenzaka Hill's street. Off by default. It changes straight away, asks more
of the computer, and is remembered as farbuildings= in the settings file
(1 or 0).

Display > Skip intro in the in-game menu leaves out the logos and the
graffiti disclaimer the game opens with: it starts at the title screen's
loading. Off by default, so the intro plays as on the Xbox. It is remembered
as skipintro= in the settings file (0 or 1).

Graphics > Anti-aliasing (MSAA) in the in-game menu smooths jagged edges:
Off (the default), 2x, 4x, or 8x, as far as the graphics card goes. It changes
straight away, with no restart, and is remembered as msaa= in the settings
file (0, 2, 4, or 8). More asks more of the graphics card, the more so at the
higher resolutions.

Graphics > Anti-aliasing (SMAA) in the in-game menu smooths jagged edges
with a quick pass over each finished frame, at much the same small cost
whatever is on screen. It smooths the HUD too. It works alone or on top of
MSAA. Off by default; it changes straight away, with no restart, and is
remembered as smaa= in the settings file (0 or 1).

Graphics > Anisotropic filtering in the in-game menu keeps textures sharp
where they are seen at a steep angle, such as streets and walls running into
the distance: Off (the default), 2x, 4x, 8x, or 16x, as far as the graphics
card goes. It changes straight away, with no restart, though the textures it
reaches take a moment to load again, and is remembered as aniso= in the
settings file (0, 2, 4, 8, or 16). It leaves things that face the screen,
such as the HUD, as they were.

Graphics > Shader cache in the in-game menu (on by default) keeps the shaders
the game builds, in the cache folder. Each time the game starts, they are all
built again before it begins, behind a progress bar, so none are built while
you play. It is remembered as shaders= in the settings file (1 or 0).
Changing it in the menu restarts the game, after asking, as a new resolution
does.

Audio > Audio effects in the in-game menu (on by default) adds our own
versions of the console's sound effects: the 3D sounds placed to your left or
right as the game sets them, the characters' voices kept on them as they
move, the reverb the game sets for a place, as in the sewers, and a limiter
of their own for the 3D sounds, so a busy moment no longer turns the music
and voices down. Turned off, the sound is as before them. It is remembered as
audioeffects= in the settings file (1 or 0). Changing it in the menu restarts
the game, after asking.

Developer > Log to file in the in-game menu (on by default) writes the game's
output to last-run.log. Turned off, nothing more is written to it until it is
turned on again, and what is there already stays. It changes straight away and
is remembered as logging= in the settings file (1 or 0). Developer > Logging
adds the engine's own diagnostic lines to the log, for bug reports,
Developer > Engine switches turns parts of the engine off or changes how they
work, to compare, Developer > Performance has GPU queue, Sleep while waiting
for the GPU, and Compressed textures direct, each on by default and ticked, to
untick and compare, and Developer > Experimental, when there is anything in
it, tries out changes to how the game runs. Each row stands for an environment
variable, kept in developer.cfg beside the settings file as NAME=1 or NAME=0,
and applies from the next start: File > Restart restarts the game, after
asking.
Developer > Purge shaders asks first, then restarts the game and deletes the
shader cache as it starts: each shader is built again when the game first
needs it.


Free camera
-----------
F8, R3 on a controller, or freecam=1 in the settings file, gives the right
stick to the camera: it turns around the player, and forward is wherever it
looks. The L trigger puts it back behind the player, as it does the game's
camera. Spraying graffiti doesn't turn it to the wall, and talking to
someone doesn't turn it to them. It steers only the camera of the skater you
control: when the game moves a skater by itself, as when a scene follows
another skater, the game's own camera shows it. Pausing keeps the view. F8
and R3 are remembered. The settings file (see "Where things are kept") also
takes these, one per line, with no spaces around "=". The values shown are
the defaults, which a key that is left out, or out of range, gets:

  freecam=0          1: on
  invert_x=0         1: right turns the view left
  invert_y=0         1: up tilts the view down
  sensitivity_x=5    left-right speed, 1 to 10: 36 degrees a second per step
  sensitivity_y=5    up-down speed, 1 to 10
  deadzone=0.05      how far the stick moves before the camera does, 0 to 1
                     (0.05 is 5%)
  autocentre=0       auto centre: once the right stick is let go, the view
                     eases back behind the player. 0 off, 1 mild (after 2
                     seconds, slowly), 2 medium, 3 high (after a second),
                     4 aggressive (after half a second, quickly)

The in-game menu's Controller menu has all of these too, and keeps them here
as F8 does.


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

Licences: MIT for this project's own code; the MCPX audio-processor code
extracted from xemu is LGPL-2.1-or-later; the bundled SDL2 library is under
the zlib licence, libepoxy under the MIT licence. See the licences folder.
Source code: https://github.com/spriteice/JSRF-PC, a fork of JuMatt's
JSRF Unleashed: https://github.com/JuMatt/JetSetRadioFuture-Unleashed
