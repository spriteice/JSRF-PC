# Changelog

Releases are tagged `vX.Y.Z` and built from that tag: the Mac app, the Windows
and Linux builds and the iPhone and Android apps say which version they are
(Finder › Get Info, `JSRF Unleashed.exe --version` on Windows,
`./jsrf-unleashed --version` on Linux, the ⚙ button on iPhone and Android),
and so does the first line of every log. A
download with nothing new for its platform is carried over unchanged and keeps
its version. Downloads are on the
[Releases](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases)
page.

## 0.5.4 — 22 October 2026

More of **spriteice**'s work from
[his fork](https://github.com/spriteice/JetSetRadioFuture-Unleashed/tree/TESTING):
fixes for the hitching some Windows players have seen, a quicker hand-over
between game threads everywhere, and what it takes to build the game from
your own disc, which could not be done before. Thank you again, spriteice!
Each of these commits links the one of his it comes from. Every platform is
new in 0.5.4.

- **Windows: the engine's short waits last as long as asked.** They went
  through winpthreads, part of the MinGW toolchain that builds the Windows
  download, which waits whole milliseconds and then until GetTickCount64,
  moving in 15.6 ms steps, says the time is up. On spriteice's PC (RTX 2080
  Ti, 120 Hz) frame pacing's 1 ms slices took 2–3 ms on average and up to
  17.5 ms, and the GPU thread's 200 µs wait took 12 ms when it timed out.
  That is the wait the game's vertical-blank handler spins on with the guest
  lock held, so every game thread waited with it: the timer thread held the
  lock 13–37% of the time, against 2–5% on Linux. A player's 0.5.3 log (RTX
  3070) has frames of 30 to 65 ms with no shader or texture work in them.
  Both waits now use a high-resolution waitable timer: on spriteice's PC,
  frames reaching the flip late went from 8–26% to 0.7%, and the timer
  thread held the lock 4.8% of the time.
- **A game thread hands the lock over without a pause.** When its time slice
  ran out with another thread queueing, it stood back with Sleep(1): a
  millisecond or more, with no game thread running for much of it. It now
  waits until the other has taken the lock, at most 1 ms
  (`RECOMP_HANDOFF_US`). On a Mac in 99th Street against 0.5.3 (Metal, 225 s
  each): the lock without a holder 1.5% of the time instead of 7.1%, 727
  frames late at the flip instead of 1,288 (of about 13,650), and 8 slow
  frames instead of 34.
- **Building from source works.** The repository now has spriteice's toolkit
  (xboxrecomp v0.15.0's and v0.16.0's pipeline, with his disassembler fixes),
  the runtime pieces the code it generates calls, and the steps in the
  README. Before, code generated from a disc could not link: the downloads'
  generated code came from an earlier version of the tools, with two tables
  (`recomp_seeded.c`, `recomp_holes.c`) made by scripts that were never
  committed. From a clean default.xbe, on a Mac and on Linux: 9333 of 9342
  functions, none failed, and the same code on both. The Mac, Linux and
  Android builds made from it build, and the Mac one plays 99th Street as
  the downloads do. The downloads stay on the older generated code until the
  new one has been played through.
- Windows: MEM_DECOMMIT of guest memory failed every time (guest RAM is one
  mapped view), about once a frame, each one a line flushed to
  `xbox_kernel.log` with the guest lock held. The pages' access is taken away
  instead, as on the other platforms.
- Mac, Linux, iPhone and Android: a read at an offset is one `pread`, not a
  seek and a read that another thread reading the same file could get
  between.
- The APU's and the NV2A's clocks no longer overflow: count × 10⁹ passed
  INT64_MAX after 15 minutes of uptime at Windows' 10 MHz counter, and after
  9 seconds at the 1 GHz the other platforms report.
- Tested on a Mac in 99th Street against 0.5.3, as above: the same missions
  reached and the same frame rate; with code generated from source, the
  same. Linux and Windows (under Wine): the game starts and runs its title
  screen; Wine does not have Windows' timer problem, so it cannot show that
  fix. The Android and iPhone builds were built and checked, not run.

## 0.5.3 — 21 October 2026

Fixes found by **spriteice**, who plays JSRF Unleashed on a ROG Xbox Ally X
and went through the engine in
[his fork](https://github.com/spriteice/JetSetRadioFuture-Unleashed/tree/TESTING).
Thank you, spriteice! Each of these commits links the one of his it comes
from, and says where it was adapted to this engine. Every platform is new
in 0.5.3.

- **The graffiti you pick now shows.** The texture cache knew a texture by
  its address, size, format and 64 bytes sampled from it. The graffiti
  picker loads each chosen design into the same 1024x1024 image, in place,
  and when those 64 bytes did not change, the preview and the posters on
  the walls kept the default graffiti. The whole content of the textures
  being drawn is now compared as well, a megabyte of them at the end of each
  frame, in turn, and a texture rewritten in place is uploaded again a few
  frames later. (Comparing all of them every frame, as first written, took
  0.6 ms a frame on a Mac; in turn, it costs nothing measurable.)
- **Video memory no longer grows at every area.** The game frees an area's
  textures when it leaves it, and the texture cache kept them on the GPU
  until all 4,096 of its slots were full: on the Ally X, from 55 MB to
  306 MB in a minute and a half. Textures whose memory the game has freed
  are dropped once a frame (on a Mac in 99th Street the cache holds 179
  textures, 57 MB), and a texture that changed is uploaded over its old
  entry rather than beside it.
- **The save folder is no longer 10 GB.** The engine backs the Xbox hard
  disk with six image files in the save folder (Partition0.img to
  Partition5.img), and made them the size of the console's partitions,
  9.77 GB together. They were sparse, so on most disks they took almost no
  space, but the folder showed as 10 GB in Explorer, in Finder and in the
  iPhone's Files app, and a copy or a backup that does not keep holes, or an
  exFAT or FAT32 drive, wrote all of it. They now hold only what the game
  writes, about 120 KB, and start empty at each launch, which also shrinks
  the old ones; nothing in them outlived a run anyway (the game formats its
  cache partition at every start). The game's saves are elsewhere, in
  `UDATA` in the game folder, and are not touched.
- **Windows: two per-thread variables were shared by every thread.** MinGW,
  which builds the Windows download, ignores `__declspec(thread)`: the
  file path the kernel had just translated, which the cutscene player reads
  back (another thread's open could replace it, and a cutscene not play),
  and the game's IRQL. Both are thread-local now, as on the other platforms.
- The game's IRQL is read from the register it is passed in (KfRaiseIrql
  and KfLowerIrql are fastcall); it was the low byte of whatever the caller
  had pushed last.
- The guest lock's time slice runs from when the lock last changed hands,
  not from the holder's latest retake after a kernel call, and for up to
  50 ms it does not end at raised IRQL or inside an interrupt, DPC or
  synchronize routine. `RECOMP_SLICE_FROM_RETAKE=1` measures it the old
  way. On spriteice's build the timer thread, which delivers the vertical
  blank and the APU interrupt, queued for the lock most of the time in
  game; on a Mac in 99th Street it made no measurable difference, the slice
  already running out once a frame.
- A draw to a surface of another size under the same viewport now uses that
  surface's size.
- Tested on a Mac in 99th Street against 0.5.2 (Metal, 225 s each, New
  Game and the left stick): the same frame rate and hitches, textures the
  same share of the frame. Linux and Windows (under Wine): the game starts
  and runs its title and attract demo, and the partition images stay small.
  The Android and iPhone builds were built and checked, not run.

## 0.5.2 — 21 October 2026 — Windows only

A second Windows-only fix for the sound, which still stuttered in 0.5.1 for
some players. The Mac, iPhone, Linux and Android downloads are 0.5.0's,
unchanged: the 0.5.2 release carries them over, and they still say 0.5.0.

- **Windows: the game's clock in 15.6 ms steps.** The engine's kernel timed
  everything on Windows' GetTickCount64, which moves in steps of the system
  tick, 15.6 ms, whatever timeBeginPeriod asks: the display interrupt that is
  the game's frame clock, the game's own timers and DPCs, and the 4 ms slice
  after which a game thread hands the guest lock to the next. On Windows
  alone, then, all of those came in 15.6 ms steps. A player's 0.5.1 log shows
  the frame clock slipping a whole step every 15 frames, a 30 ms frame four
  times a second: a 60 Hz clock read off a 64 Hz tick. The 4 ms slice became
  15.6 ms, so the ADX server thread that refills the music waited about four
  times as long for its turn, and the music ran dry.
- On Windows the kernel's GetTickCount and GetTickCount64 are now the
  performance counter in milliseconds, as they are CLOCK_MONOTONIC on the
  other platforms. The engine no longer imports either.
- The game asks Windows 11 not to throttle it when its window is covered or
  minimised, neither its speed nor its timer resolution (on Windows 10, the
  speed alone): the Windows side of the Mac's App Nap opt-out.
- 0.5.1's fix (every game thread at normal priority on Windows) is kept.
- Tested under Wine, where the game starts and runs its title screen as
  before; the stutter itself needs a real Windows PC to show.

## 0.5.1 — 21 October 2026 — Windows only

A Windows-only fix. The Mac, iPhone, Linux and Android downloads are
0.5.0's, unchanged: the 0.5.1 release carries them over, and they still say
0.5.0.

- **Windows: sound that stutters when the PC is busy.** The game gives each
  of its threads a priority, as Xbox games do, and on Windows those were
  applied to real Windows threads; on the Mac and Linux, SetThreadPriority
  is the engine's own and only remembers the number. The game puts CRI's ADX
  worker, the thread that reads and decodes the music stream, at the lowest
  priority, idle. On the console that thread runs whenever the others wait,
  half of every frame; on a PC it runs only when no normal-priority thread
  anywhere wants its core (the game's own threads, a browser, a chat app),
  so under load it starved and the music ran dry. The game's other requests
  were applied too: highest and above normal for three threads, and one
  thread raising itself to time-critical and back about fifty times a
  second.
- Every game thread now runs at normal priority on Windows, as on the other
  platforms, and the game reads back the priorities it set.
  `RECOMP_THREAD_PRIORITIES=1` applies them as before.
- The APU's frame thread, which makes the sound 5 ms at a time and sleeps
  in between, runs at the highest priority below time-critical on Windows,
  so that a busy moment delays the picture rather than the sound (SDL's
  device thread, which plays what it makes, was at time-critical already).
- Tested under Wine, where the game starts and runs its title screen as
  before; Wine does not apply thread priorities, so the stutter itself could
  not be reproduced there. The game's requests were logged on Linux.

## 0.5.0 — 21 October 2026

- **Less stutter: new textures appear about six times faster.** The first
  time the game uses a texture it is converted on the CPU, and a new area
  brings dozens at once: when 99th Street starts, 59 arrive in one frame and
  65 in another, and each was a freeze of about a fifth of a second; new
  parts of an area coming into view mid-play cost smaller ones. A compressed
  (DXT) texture was decoded per pixel, decoding its whole 4×4 block for every
  pixel of it, and a swizzled one worked out where each pixel lives from
  scratch every time. Each block is decoded once now, and the layout is worked
  out once per texture: the same 59 textures take about 35 ms, with exactly
  the same pixels.
- **Less stutter: shaders are kept, and built before the game needs them.**
  The first time the game draws something new, its shaders are built and
  the game waits. Every shader built is now kept, in a `shaders` folder in
  the app's own data folder (on a Mac, `~/Library/Application Support/JSRF
  Unleashed/save`; not the game folder), and at the next start they are built
  again before they are needed: on Metal by two background threads while the
  title screen runs, with the pipeline states each was drawn with; on OpenGL
  a few milliseconds at a time on frames with little to draw (menus, loading
  screens). Where the driver hands out compiled programs (Windows, Linux,
  Android), each is kept compiled, for that driver, and loaded instead of
  built. With OpenGL on a Mac, which keeps nothing between sessions, four
  minutes on 99th Street spent about 430 ms building shaders, and now about
  50 ms; on Linux (Mesa) a shader that took 13.8 ms loads in 1.5 ms. On Metal,
  macOS keeps compiled shaders itself, so the gain there is small. A shader
  needed for the first time on a machine is still built on the spot. The
  folder can be deleted at any time; `RECOMP_SHADER_CACHE=0` turns the cache
  off and `RECOMP_SHADER_BINARIES=0` keeps sources only.
- **No more grey spikes.** Some objects shot long grey spikes across the
  screen for a moment, for instance on the staircase that winds around the
  garage's centre platform. The reciprocal step of the game's vertex
  programs (RCC) lost its sign, so points just behind the camera were thrown
  across the screen; it keeps its sign now, its size held between 2^-64 and
  2^64 as on the console, the perspective divide never meets a zero, and the
  address register is set as the console sets it.
- **Mac: no stutter with the window covered.** macOS slows down the timers
  of an app whose window is covered (App Nap), and the game paces itself,
  streams and plays its music on timers: a covered window stuttered in
  quarter-second stalls. The game declares itself latency-critical work for
  as long as it runs.
- **The log says what each stutter was.** Every frame that took one and a
  half frames or longer is written to `last-run.log` (`[HITCH]`) with the
  shaders built in it, the textures converted and the time left to the game
  itself; the first 200, then one in 50, and a total at the end.
- Tested on the Mac with Metal and OpenGL: New Game, then 99th Street,
  four minutes of skating, each renderer with and without the shader cache
  and with the old and the new texture conversion; the final Mac app ran the
  title on both. The new texture conversion was compared with the old one
  pixel for pixel on DXT1, DXT3 and DXT5 at many sizes, and the swizzle
  offsets on every power-of-two size up to 2048×2048. The shader cache also
  ran on Linux (Mesa) with its files empty, filled and damaged; the final
  Windows build ran under Wine twice, its shaders built and saved the first
  time and loaded already compiled the second, and the final Linux build ran
  on Mesa. The Android and iPhone builds were built and checked, not run.

## 0.4.9 — 20 October 2026

- **The character select shows the skaters where they are.** Each rudie
  on the select screen skates in two small windows, and there they came
  out in the wrong place: too big, too low and too far right, sunk into the
  floor or under the rail they were grinding with the sparks alone on it,
  and in the small window often out of frame. The game draws those windows
  as viewports inside the screen. The scenery and the effects go through
  the console's fixed-function path, which the renderer placed right; the
  skaters are drawn by the game's own vertex programs, whose output the
  renderer stretched over the whole screen, as if each window were the
  screen. They are put back in their windows now. Everything drawn full
  screen, which is every stage, comes out exactly as before.
- **Mac: the game can be set up again.** In "Pick the folder of extracted
  game files, or a disc image (.iso)", choosing the folder and clicking
  Use This only opened it, and inside it the button stayed grey: the window
  accepted .iso files and nothing else. And an .iso was handed to
  extract-xiso, which is not part of macOS, so without Homebrew the app
  could only say to install it. Folders and disc images can both be chosen
  now, and the app unpacks an image by itself, with a progress bar, using
  the unpacker the iPhone, Android and Windows apps already had (the engine
  does it: `--unpack-iso`). A folder with the image in it works too, and
  either can be dropped on the app's icon or given on its command line.
- **Linux: disc images without extract-xiso.** The launcher unpacks an
  .iso by itself as well, with a progress window when zenity is there, so
  nothing has to be installed, on a Steam Deck either.
- **Wrong files are named, before anything is unpacked.** JSRF Unleashed
  is made from one build of the game, address for address: the standalone
  US disc. A player whose files were another build saw the game crash a few
  seconds in (a "misaligned access" on a Mac, with 0.4.6). Every app now
  checks the files first and says what they are instead: another game's
  disc (by its name), a zip or other archive to unpack first, a compressed
  disc image, a disc with no game on it, or another build of Jet Set Radio
  Future -- the European or the Japanese release, or the Sega GT 2002 +
  JSRF combo disc, the copy that came with the Xbox, whose JSRF is a later
  build than the standalone disc's. A disc image is checked before it is
  unpacked (the engine reads its default.xbe out of it: `--check-iso`).
  Only the layout of default.xbe is compared, not the bytes, so a US
  default.xbe with a patch applied (the region, a widescreen hack) still
  runs.
- Tested on the Mac with Metal and OpenGL: the character select from a save,
  and the garage after it, unchanged. The Mac app unpacked the game's disc
  image in a few seconds, all 3,221 files identical to the extracted game's,
  and named another game's image without unpacking it; the check was also
  run on a zip, on a file that is no disc image, and on copies doctored to
  look like a European release and a combo disc, and the Linux launcher
  (with its progress window) and the Windows launcher (under Wine) refused
  the same wrong files. Earlier builds of this version ran on Mesa, under
  Wine and in the Android emulator; the final Android and iPhone builds were
  built and checked, not run.
- Every download is 0.4.9: Mac, iPhone, Linux, Windows and Android.

## 0.4.8 — 20 October 2026

- **The robot at the future site of the Rokkaku Expo Stadium starts.** A
  player on Android 0.4.7 could not finish the Urban Battle there ("Destroy
  the Rokkaku piece of junk!"): the robot hung where it had been made, out
  of reach, and never moved, attacked or could be tagged. Every earlier
  version had the same soft lock. The robot's first step, the one that puts
  it in the stadium and starts it, was one of four functions of the game
  that the translation had never found. Each of the four sits right after
  another function, and the recompiler took it for the end of that one, so
  no earlier scan saw it as missing. The game called it every frame and
  every call was skipped. It is in now: the robot fights at ground level,
  moves around the stadium and attacks, and its arms can be grinded and the
  marks on it sprayed.
- **The Rokkaku robots make their sounds.** Their sound function was another
  of the four. The Terror Drone at Hikage Street calls it four times a frame
  for its thrusters; every call was skipped, so the robots were silent.
- The other two are handlers in two of the game's per-object tables, each
  placed right after a jump table that the recompiler read as code. Nothing
  has been seen calling them yet.
- Found by listing every address that the game's tables or its code's
  constants hold, that follows the end of a function, and that no
  translated function starts at — inside other functions this time.
- Tested on the Mac with the game code of 0.4.7 and of 0.4.8. With 0.4.7's,
  the robot at the future site never leaves its first state and the game
  calls the missing function every frame; with 0.4.8's it goes through its
  whole routine, and a test player put on its arms grinded them and sprayed
  one of its marks off. The Terror Drone at Hikage Street, which worked
  before, was sprayed the same way with both and moved on after each mark.
  The Linux build was started on Mesa, the Windows build under Wine and the
  Android build in the Android emulator.
- Every download is 0.4.8: Mac, iPhone, Linux, Windows and Android.

## 0.4.7 — 15 October 2026 — Android only

An Android-only fix. The Mac, iPhone, Linux and Windows downloads are
0.4.6's, unchanged: the 0.4.7 release carries them over, and they still say
0.4.6.

- **On-screen controls and controllers on phones that call one of their
  own parts a gamepad.** Two players saw the intro play with no
  on-screen controls, and their Bluetooth controllers (an Xbox Series X
  controller, an 8BitDo Ultimate 2) did nothing. Many phones list one of
  their own parts as a gamepad: the device their fingerprint reader or side
  buttons send keys through declares gamepad buttons. The app counted it as
  a controller and hid the on-screen controls, and SDL took it for one too:
  it was opened first, as player 1's controller, and a real controller went
  in as player 2's, which the game does not read.
- A device now counts as a controller only if it has a stick, or a d-pad
  that reports as one (SDL's own rule for this, which the app now turns
  on). On those phones the on-screen controls show, and a controller
  connected to them is player 1's.
- The log says what the app made of each device Android calls a gamepad
  (its name, how many axes it has, whether it counts as a controller) and
  whether the on-screen controls are showing.
- Tested in the Android emulator with a stand-in for such a phone part,
  there from the start, and a controller connected 20 seconds in that
  pressed Start and A. With 0.4.6 the on-screen controls were missing and
  the controller did nothing. With 0.4.7 the controls showed, went away
  when the controller connected, and the controller started the game.

## 0.4.6 — 15 October 2026

- **Skyscraper District and Pharaoh Park: the city is back.** A player on a
  Steam Deck saw his character there and nothing else: the whole city around
  him was black. These areas switch the console's fog on (black, from 300 to
  3000 units away), and the engine got fog wrong for the scenery, which the
  game draws with the console's fixed-function transform: every one of its
  vertices was given a fog factor of zero, which paints it entirely in the
  fog colour. Black fog, black city.
- **The areas with a coloured fog** had the same bug, in their own colour:
  three of the later areas came out as a flat wall of pinkish brown or olive,
  with the characters and a few objects standing in it.
- Fog is now worked out the way the console does it. The fixed-function
  transform measures each vertex's distance from the camera the way the game
  asks it to, the game's own vertex programs supply theirs as before, and
  both become a fog factor through the fog mode and the start and end
  distances the game sets. Distant buildings fade into the night or the haze
  as they should, instead of the scenery vanishing, and the scenery the game
  draws with its own programs, which never faded at all, fades too.
- The renderer is the same code on every platform (Metal on the Mac and
  iPhone, OpenGL on Linux, Windows and Android), so every download has the
  fix.
- Tested in Pharaoh Park with the Mac build on OpenGL, the renderer of the
  Linux and Windows builds, and on Metal, and in the iPhone Simulator, which
  also went through one mission in each of 13 areas, from Shibuya Terminal
  to Pharaoh Park: no crash, no shader that failed to compile, the three
  areas with a coloured fog drawn as they should be, and the areas without
  fog exactly as before. The Linux build was started on Mesa and the
  Windows build under Wine, every shader compiling, and the Android build
  ran in the Android emulator.
- Every download is 0.4.6: Mac, iPhone, Linux, Windows and Android.

## 0.4.5 — 15 October 2026

- **Chuo Street: no more crash at the third tank.** A player's log showed
  the game crashing every time he tagged his third tank. The tanks' sound
  function was one of 27 functions of the game that the translation had
  never found: their addresses appear only as constants in the game's code
  (written into an object, into a table filled at startup, or handed to a
  new thread), which neither the recompiler nor the later scans of the
  game's tables looked at. Each call to it was skipped, and the skip left
  the stack 8 bytes off; when the third tank was tagged, a tank routine came
  back 16 bytes off and the game crashed. The 27 are translated now.
- **Sounds that never played.** 17 of the 27 are sound functions, one for
  each of 17 kinds of objects, the tanks among them. Whatever sounds they
  start never played until now.
- **A missing function costs less.** A call into code that was never
  translated is still skipped, but the stack is now left the way the calling
  code expects, so the next missing function costs a sound rather than the
  game. The log names each one in full four times, then once every thousand
  calls (the player's log was 1.4 MB of the same lines).
- Tested in the iPhone Simulator, jumping straight into the tank fight and
  skating toward the tanks with the same script on 0.4.4 and on 0.4.5:
  0.4.4 skipped the tanks' sound function 793 times; 0.4.5 skipped nothing,
  started 329 sounds against 211 and did not crash. The third tank itself
  has not been reached in a test yet.
- Every download is 0.4.5: Windows, Linux, Mac, Android and iPhone. The
  Mac download, 0.2.0's until now, and the Linux one, 0.3.1's, have every
  change since that applies to them.

## 0.4.4 — 15 October 2026

- **iPhone: the picture can fill the screen.** At 16:9 the game has black
  bars at both ends of a 19.5:9 iPhone. **⚙ › Picture › Fill the screen**
  widens the view to the screen's own shape, so the city reaches both edges,
  while the menus and the HUD keep their shape in the middle. **16:9** stays
  the default, and the console's **4:3** is the third choice; the title
  screen stays 4:3 in all three. An iPad's screen is narrower than 16:9, so
  there Fill the screen is 16:9. Run in the iOS Simulator (an iPhone 16 Pro:
  a new game, the first scenes).
- The picture is a new setting, so if you had switched widescreen off, the
  app starts at 16:9 once after the update: pick 4:3 in **⚙ › Picture** to
  have it back.
- **iPhone: a stability fix the other versions already had.** A repair for
  registers some of the game's translated functions fail to preserve, whose
  absence can crash a long session, was never switched on in the iPhone app:
  its settings were set too late for the part of the engine that reads them.
  It is on from the start now (the first line of `Logs/last-run.log` says
  so).
- The iPhone app is updated through AltStore and SideStore: it shows up in
  **My Apps** as an update, and keeps the game and your saves.
- The Windows download is 0.4.2's, the Android download 0.4.3's, the Linux
  download 0.3.1's and the Mac download 0.2.0's, unchanged.

## 0.4.3 — 14 October 2026

- **Android: the faster renderer** from 0.4.2. Geometry that does not change
  stays on the graphics chip between frames instead of being converted and
  sent again every frame, and the game's constants are sent only when they
  change. On a phone, where moving memory costs the most, that was most of
  the renderer's own work in the busy scenes. Played in the Android emulator;
  not yet on a phone.
- The Windows download is 0.4.2's, the Linux download 0.3.1's, and the Mac
  and iPhone downloads are 0.2.0's, unchanged; AltStore keeps offering 0.2.0.

## 0.4.2 — 14 October 2026

- **Windows: Xbox controllers work.** An Xbox pad was found and opened but
  its input never arrived: SDL hands it to a hidden window, which belonged to
  the game's own thread, and that thread never reads its messages. SDL now
  keeps a thread of its own for it, which also finds pads plugged in later.
  The log lists the input devices SDL sees (`[PAD]` lines in
  `last-run.log`).
- **Windows: a Video menu** at the top of the window, as on the Mac: the
  resolution (1× to 4×), widescreen and full screen. It is hidden in full
  screen; **F9**, **F10** and **F11** still work everywhere. F9 in full
  screen no longer looks like a hang: its box was hidden behind the game
  until Alt+F4 closed it, so the resolution never changed. Full screen is now
  left while the box is up.
- **A faster renderer** (in the Windows build; the Linux and Android ones get
  it when they are next updated). Geometry that does not change stays on the
  graphics card between frames instead of being converted and sent again
  every frame (about 15 MB a frame in the busy scenes), and the game's
  constants are sent only when they change: the renderer's own work in a
  busy frame went from about 9 ms to under 2 on the test machine, which
  leaves more of each sixtieth of a second to the game. Played under
  CrossOver on a Mac.
- The Android download is 0.4.0's, the Linux download 0.3.1's, and the Mac
  and iPhone downloads are 0.2.0's, unchanged; AltStore keeps offering 0.2.0.

## 0.4.1 — 14 October 2026

- **Windows: smoother frames.** Each frame now reaches the screen in step
  with the display, from a thread of its own, as the Metal renderer does it
  on the Mac, and the game is paced to the display's refresh: on a 60 Hz
  screen (and on 120 or 240 Hz ones) each frame is shown once. Before, the
  game's own clock and the display's drifted apart, and every few seconds a
  frame stayed up twice and the next was not shown at all. Played under
  CrossOver on a Mac; not yet on a Windows PC. The log now says every 10
  seconds how many frames were shown and how long each stayed on screen
  (`[PRESENT]` lines in `last-run.log`).
- The Android download is 0.4.0's, the Linux download 0.3.1's, and the Mac
  and iPhone downloads are 0.2.0's, unchanged; AltStore keeps offering 0.2.0.

## 0.4.0 — 14 October 2026

- **Android** (new): `JSRF-Unleashed-android-arm64.apk`, for
  Android 10 and later on 64-bit phones and tablets with OpenGL ES 3.0. Its
  first screen unpacks your disc image (`.iso`) into the app, or takes the
  extracted game folder copied over USB. Controllers through SDL, on-screen
  controls as on iPhone (a stick, the pad's buttons, Start and Back at the
  top), a keyboard; Android's Back is the Xbox Back button. The ⚙ button has
  widescreen, the on-screen controls and the resolution. The renderer is the
  Windows and Linux one, in OpenGL ES. Played in the Android emulator
  (unpacking a disc image, boot, title, attract demo, the controls and the
  settings); not yet on a phone.
- The Windows download is 0.3.0's, the Linux download 0.3.1's, and the Mac
  and iPhone downloads are 0.2.0's, unchanged; AltStore keeps offering 0.2.0.
- Building: `launcher/android/` has the app and `build_apk.sh`, which builds
  it with the Android SDK's own tools (the NDK, aapt2, d8; no Gradle).

## 0.3.1 — 14 October 2026

- Linux: the F9 resolution window's text is no longer cut off at the right
  edge (SDL's own dialog on X11 does not wrap long lines).
- The Windows download is 0.3.0's, and the Mac and iPhone downloads are
  0.2.0's, unchanged.

## 0.3.0 — 14 October 2026

- **Windows** (new, experimental): `JSRF-Unleashed-windows-x86_64.zip`, for
  Windows 10 and 11, 64-bit. Unzip it and run `JSRF Unleashed.exe`: it asks
  for the game folder, or a disc image, which it unpacks once. The same engine
  and OpenGL renderer as the Linux build, controllers through SDL, the
  keyboard. Played under CrossOver on a Mac and under Wine on Linux (boot,
  title, loading a save, the garage); not yet on a Windows PC.
- On Windows and Linux, **F9** sets the resolution — 1× to 4× the Xbox's
  640×480, up to 2560×1920 — and restarts the game to apply it; **F10**
  switches widescreen and **F11** full screen. The resolution and widescreen
  are remembered (on Windows, widescreen was forgotten at every start).
- **Linux**: rebuilt from the 0.3.0 tag for F9; nothing else changes for it.
  (For the first hour after the release, the Linux download was still 0.2.0's
  build.)
- The Mac and iPhone downloads are 0.2.0's, unchanged (they call themselves
  0.2.0); AltStore keeps offering 0.2.0.
- Docs: saves are kept in the game folder, in `UDATA`, on every platform — not
  in Application Support (Mac), `~/.local/share` (Linux) or `Saves` (iPhone),
  as the README said.
- Building: `launcher/windows/` has the Windows launcher, its packaging, and
  the toolchain file for cross-compiling with llvm-mingw.

## 0.2.0 — 12 October 2026

- **iPhone and iPad** (new, experimental): `JSRF-Unleashed-iOS.ipa`, for
  sideloading (AltStore, SideStore, Sideloadly). The game goes into the app's
  folder in the Files app — the extracted folder, or the disc image, which the
  app unpacks itself. Controllers, on-screen controls (a stick and the pad's
  buttons), a hardware keyboard. Played in the iOS Simulator; not yet tried
  on a real device.
- **Mac: Metal is the renderer now.** Each frame goes straight to the window,
  every one of them reaches the screen, and on a 120 Hz display (a MacBook
  Pro's) nearly every frame stays up for exactly a sixtieth of a second:
  smooth 60 fps. Less CPU, too. OpenGL is still there, under Video › Renderer.
- **Fixed: the picture could freeze for good** now and then during play (all
  platforms). The game's graphics driver sometimes waits for an interrupt the
  GPU raises when it reaches a marker in the command stream; nothing raised
  it.
- Widescreen: on the character select the characters stay inside their
  panels; the boot logos fill the 16:9 screen.
- Fixed: a 1-pixel dark line along the right and bottom edges.
- Logs are a small fraction of the size (most of a log was one line per GPU
  submission).
- Building on a Mac needs nothing from Homebrew.

## 0.1.2 — 2 October 2026

No change to the game.

- Both downloads are built from the tagged source and carry its version. (The
  0.1.0 and 0.1.1 Mac apps call themselves "0.1".)
- The repository builds the game on its own: `port/CMakeLists.txt`, given the
  generated code in `port/recomp/gen`.
- `launcher/package_mac.sh` and `launcher/linux/package.sh` make the release
  downloads.

## 0.1.1 — 2 October 2026

- Linux: New Game no longer stays on "Now Loading" for good. The game asks for
  its files in a different case from the one on the disc
  (`TypeA.bin` / `typea.bin`); file names are now found whatever their case.
- The Mac app is the same as in 0.1.0.

## 0.1.0 — 2 October 2026

First public release.

- macOS app for Apple Silicon, and a first Linux (x86_64) build — whose New
  Game hangs on "Now Loading"; use 0.1.1 or later.
- Playable from New Game through the tutorial, the DJ K scene and Dogenzaka
  Hill: skating, grinding, tricks, graffiti, the pause map, music, sound
  effects and voices, saving and loading.
- Controllers and the keyboard.
- Widescreen 16:9 with a wider view, not a stretch; the HUD keeps its shape,
  the title screen stays 4:3.
