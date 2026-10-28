JSRF-PC for Linux (x86_64)
==========================

Jet Set Radio Future, statically recompiled and running natively.
https://github.com/spriteice/JSRF-PC

You need your own copy of the game -- the standalone US disc (not the Sega GT
2002 + JSRF combo disc, whose JSRF is a different build). This package is the
engine only: no game files, no textures, no music, no video.


What you need
-------------
- A 64-bit PC (x86_64) with a recent Linux: glibc 2.34 or later, which means
  Ubuntu 22.04, Debian 12, Fedora 36, Linux Mint 21, current Arch, SteamOS,
  or anything newer.
- Graphics drivers with OpenGL 3.3 (Mesa or NVIDIA's, on any GPU from the last
  ten years).
- A desktop session, X11 or Wayland. Sound goes through PipeWire, PulseAudio,
  or ALSA.
- Your game files: an image of your own disc (an XISO or a full disc image,
  usually .iso), or the folder its files were extracted into -- the one with
  default.xbe, Media/, and the rest in it.


Playing
-------
1. Extract this archive anywhere.
2. Run ./jsrf-pc
3. The first time, it asks where the game is: pick the folder that contains
   default.xbe, or the folder your disc image (.iso) is in. (With zenity or
   kdialog installed you get a folder picker; otherwise it asks in the
   terminal.) An image is unpacked once, by the game itself -- nothing else to
   install. It remembers the choice. You can also give the folder or the
   image on the command line:  ./jsrf-pc /path/to/game.iso
   If the files are not the right ones (another build of the game, another
   game's disc, a zip), it says what they are.

   ./jsrf-pc --install     adds JSRF-PC to your applications menu
   ./jsrf-pc --scale N     internal resolution, 1 to 4 (default 2); remembered
   ./jsrf-pc --fullscreen  always starts full screen
   ./jsrf-pc --choose      asks where the game is again
   ./jsrf-pc --version     which version this is


Controls
--------
Controller: any pad SDL knows -- Xbox, PlayStation, Switch Pro, Steam Deck,
and many more.

Keyboard (by key position, so W A S D on QWERTY is Z Q S D on AZERTY):

  W A S D      move (left stick), menus     Arrow keys  camera (right stick)
  Space or J   A -- jump                    K           B
  H            X                            L           Y
  E            R trigger -- talk, spray     Q           L trigger
  R            Black                        F           White
  Return       Start                        Esc         Back
  C / V        left / right stick click

  F1                  the in-game menu (also Back on a controller, which is
                      then the menu's alone): File, Display, Graphics,
                      Controller, and Developer
  F11 or Alt+Return   full screen; remembered
  F10                 widescreen 16:9 (a wider view, not a stretch; the HUD
                      keeps its shape, the title screen stays 4:3); remembered
  F9                  resolution: the game is drawn at 1x to 4x the Xbox's
                      640x480 (2x by default), up to 2560x1920; remembered.
                      The game restarts to apply it, so save first.
  F8                  free camera: the right stick turns the camera around
                      the player; remembered (see Free camera below)

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

Developer > Log to file in the in-game menu (on by default) writes the game's
output to last-run.log. Turned off, nothing more is written to it until it is
turned on again, and what is there already stays. It changes straight away and
is remembered as logging= in the settings file (1 or 0). Developer > Logging
adds the engine's own diagnostic lines to the log, for bug reports,
Developer > Engine switches turns parts of the engine off or back to how they
were, to compare, Developer > Performance runs the game on the P cores, the
fast ones (off by default), or turns off the GPU queue or display pacing, to
compare, and Developer > Experimental, when there is anything in it, tries out
changes to how the game runs. Each row stands for an environment variable,
kept in developer.cfg beside the settings file as NAME=1 or NAME=0, and applies
from the next start: File > Restart restarts the game, after asking.
Developer > Purge shaders asks first, then restarts the game and deletes the
shader cache as it starts: each shader is built again when the game first
needs it.


Free camera
-----------
F8, or freecam=1 in the settings file, gives the right stick to the camera:
it turns around the player, and forward is wherever it looks. The L trigger
puts it back behind the player, as it does the game's camera. Spraying
graffiti doesn't turn it to the wall, and talking to someone doesn't turn it
to them. F8 is remembered. The settings file (see "Where things are kept")
also takes these, one per line, with no spaces around "=". The values shown
are the defaults, which a key that is left out, or out of range, gets:

  freecam=0          1: on
  invert_x=0         1: right turns the view left
  invert_y=0         1: up tilts the view down
  sensitivity_x=5    left-right speed, 1 to 10: 36 degrees a second per step
  sensitivity_y=5    up-down speed, 1 to 10
  deadzone=0.05      how far the stick moves before the camera does, 0 to 1
                     (0.05 is 5%)

The in-game menu's Controller menu has all of these too, and keeps them here
as F8 does: with no keyboard (Steam's Game Mode), Back opens it. Or bind F8
to a controller button in Steam Input; the right stick click is free, as the
game does not use it.


Where things are kept
---------------------
  Saves     in the game folder, under UDATA (where an Xbox keeps them): copy
            UDATA somewhere to back your progress up
  Settings  ~/.config/jsrf-pc/settings
  Cache     ~/.local/share/jsrf-pc/save
  Logs      ~/.local/state/jsrf-pc/last-run.log (and logs/)


Portable mode
-------------
Make a folder named "portable" next to jsrf-pc, and everything is kept
in it instead -- nothing in your home folder, nothing in the game folder:

  portable/settings.cfg   settings
  portable/saves/         saves (UDATA and TDATA): back this folder up
  portable/logs/          last-run.log and the logs before it
  portable/cache/         the game's cache: safe to delete
  portable/games/         disc images unpacked in portable mode

The first time, it asks where the game is again, and copies the saves it
finds in the game folder into portable/saves (the originals stay). A game
folder inside this folder is remembered relative to it, so the whole folder
can be moved, to an SD card for instance.

To update, extract the new version over this folder, or move portable/ into
the new one: deleting the folder deletes your saves with it. --install does
not work in portable mode (the applications menu is in your home folder).
Graphics drivers, sound, and the folder picker keep their own small files in
your home folder as they do for any program.

Found a bug? Open an issue on GitHub with last-run.log and what you were doing.


Steam Deck
----------
Not tested yet, but it should work: in Desktop Mode, run ./jsrf-pc once
to pick the game folder, then add the jsrf-pc script to Steam as a
non-Steam game.


Legal
-----
Jet Set Radio Future is (c) SEGA. This is an unofficial fan project, not
affiliated with or endorsed by SEGA. This package contains the engine -- the
runtime and the game's code, machine-translated to native code -- and no game
data.

Licences: MIT for this project's own code; the MCPX audio-processor code
extracted from xemu is LGPL-2.1-or-later; the bundled SDL2 library is under
the zlib licence. See the licences/ folder. Source code:
https://github.com/spriteice/JSRF-PC, a fork of JuMatt's JSRF Unleashed:
https://github.com/JuMatt/JetSetRadioFuture-Unleashed
