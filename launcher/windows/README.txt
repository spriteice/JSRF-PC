JSRF Unleashed for Windows (x86_64)
===================================

Jet Set Radio Future, statically recompiled and running natively.
https://github.com/JuMatt/JetSetRadioFuture-Unleashed

You need your own copy of the game -- the standalone US disc (not the Sega GT
2002 + JSRF combo disc, whose JSRF is a different build). This package is the
engine only: no game files, no textures, no music, no video.

The Windows build is the same engine as the Mac and Linux builds, tested
under Wine. Bug reports are very welcome.


What you need
-------------
- Windows 10 or 11, 64-bit.
- Graphics drivers with OpenGL 3.3 (any GPU from the last ten years, with its
  maker's driver -- the one Windows Update installs is usually fine).
- Your game files: the folder with default.xbe, Media\ and the rest in it,
  extracted from your own disc -- or a disc image (.iso) of it, which the
  launcher unpacks once.


Playing
-------
1. Unzip this archive anywhere. Keep the engine folder beside the .exe.
2. Run "JSRF Unleashed.exe".
3. The first time, it asks where the game is: pick the folder that contains
   default.xbe -- or the folder your .iso is in, and it offers to unpack it
   (once, into %LOCALAPPDATA%\JSRF Unleashed). It remembers the choice.

Windows may warn that the app is from an unknown publisher (it is not signed:
that costs money every year). Click "More info", then "Run anyway".

You can also drop a game folder or a .iso onto "JSRF Unleashed.exe", or use
it from a command prompt:

  "JSRF Unleashed.exe" --scale N     internal resolution, 1 to 4 (default 2; F9
                                     in game does the same); remembered
  "JSRF Unleashed.exe" --fullscreen  starts full screen
  "JSRF Unleashed.exe" --choose      asks where the game is again
  "JSRF Unleashed.exe" --version     which version this is


Controls
--------
Controller: any pad SDL knows -- Xbox, PlayStation, Switch Pro and many more.

Keyboard (by key position, so W A S D on QWERTY is Z Q S D on AZERTY):

  W A S D      move (left stick), menus     Arrow keys  camera (right stick)
  Space or J   A -- jump                    K           B
  H            X                            L           Y
  E            R trigger -- talk, spray     Q           L trigger
  R            Black                        F           White
  Enter        Start                        Esc         Back
  C / V        left / right stick click

  F11 or Alt+Enter   full screen
  F10                widescreen 16:9 (a wider view, not a stretch; the HUD
                     keeps its shape, the title screen stays 4:3); remembered
  F9                 resolution: the game is drawn at 1x to 4x the Xbox's
                     640x480 (2x by default), up to 2560x1920; remembered.
                     The game restarts to apply it, so save first.

The Video menu at the top of the window has the same three. It is hidden in
full screen, where the keys still work.


Where things are kept
---------------------
  Saves     in the game folder, under UDATA (where an Xbox keeps them), so
            the game folder has to be one you can write to. Copy UDATA
            somewhere to back your progress up.
  Settings  %LOCALAPPDATA%\JSRF Unleashed\settings
  Cache     %LOCALAPPDATA%\JSRF Unleashed\save
  Logs      %LOCALAPPDATA%\JSRF Unleashed\last-run.log (and logs\)

If the game stops unexpectedly, the launcher says so and offers to show the
log. Found a bug? Open an issue on GitHub with last-run.log and what you were
doing.


On a Mac or Linux, through Wine or CrossOver
--------------------------------------------
There are native Mac and Linux builds, which are the better choice. The
Windows one should run under Wine too: in CrossOver, make a Windows 10
64-bit bottle and use "Run Command..." on JSRF Unleashed.exe.


Legal
-----
Jet Set Radio Future is (c) SEGA. This is an unofficial fan project, not
affiliated with or endorsed by SEGA. This package contains the engine -- the
runtime and the game's code, machine-translated to native code -- and no game
data.

Licences: MIT for this project's own code; the MCPX audio-processor code
extracted from xemu is LGPL-2.1-or-later; the bundled SDL2 library is under
the zlib licence, libepoxy under the MIT licence. See the licences folder.
Source code: https://github.com/JuMatt/JetSetRadioFuture-Unleashed
