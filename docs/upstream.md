# Upstream

JSRF-PC is a fork of
[JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed), by
JuMatt. It keeps upstream's releases on a branch of their own, and takes
the changes it chooses from them, one commit at a time, fitted to its own
code. Its targets are Linux first, then Windows. This file says how
upstream's changes are taken, how JSRF-PC may differ from upstream, and
lists every difference.

## Branches

- **`upstream`:** upstream's history, never edited. It moves only forward,
  to an upstream release, when the maintainer asks.
- **`main`:** JSRF-PC. Upstream's changes reach it only as commits picked
  from `upstream`, never by a merge. The last release merged into it was
  0.5.6 (`4d31983`).

## Taking upstream changes

Two separate steps, each only when the maintainer asks: moving `upstream`
to a new release, and picking commits from it into `main`.

### Moving `upstream` to a release

Only `upstream` changes: nothing is built, tested, or checked against
`main`.

1. **Bring `upstream` up to `origin/upstream`** as a fast-forward.
2. **Move it** to the release's "Changelog: 0.5.5" commit, as a
   fast-forward. Upstream tags the release (`v0.5.5`) a commit or two
   before it, so check that the tag is in its history. If it can't
   fast-forward, upstream has rewritten its history: stop and ask.

   ```sh
   git fetch origin upstream
   git checkout upstream && git merge --ff-only origin/upstream
   git fetch https://github.com/JuMatt/JetSetRadioFuture-Unleashed.git main tag v0.5.5
   git log --oneline -3 FETCH_HEAD     # the "Changelog: 0.5.5" commit
   git merge --ff-only <that commit>
   ```

3. **Push `upstream`** when the maintainer says: `git push origin upstream`.

### Picking commits into `main`

- **Chosen commits.** The maintainer says when, and which of `upstream`'s
  commits `main` takes. The others stay on `upstream`, and can still be
  taken later.
- **Picked one at a time.** Each chosen commit is cherry-picked with `-x`,
  which keeps its author and message and adds the upstream commit it came
  from.
- **Our code comes first.** Where a picked commit meets our code, ours
  stays and upstream's change is fitted around it, as each entry's "When
  picking" says. Any part of the commit that our code already does is left
  out. Where upstream's version could replace one of our entries, the
  maintainer decides.
- **A commit that needs an earlier one** is picked with it, or fitted
  without it: the proposal says which.

Steps:

1. **Start from the latest `origin/main`,** brought up as a fast-forward.
   If `main` is ahead of `origin/main`, those are unpushed commits: say so,
   and ask before building on them.

   ```sh
   git fetch origin main upstream
   git checkout main && git merge --ff-only origin/main && git status -sb
   ```

2. **List the commits not picked yet** for the maintainer, oldest first:
   what each one changes, the entries below that it meets, and whether it
   picks cleanly. The commands list `upstream`'s commits since 0.5.6, those
   already picked, and the files a commit would conflict in, if any. None
   of them changes a branch:

   ```sh
   git log --reverse --oneline 4d31983..origin/upstream
   git log --oneline --grep='cherry picked from commit' 4d31983..main
   git merge-tree --write-tree --merge-base=<commit>^ main <commit>
   ```

3. **Pick each chosen commit,** oldest first, without committing yet:
   `git cherry-pick -n -x <commit>`. Resolve each conflict with our code
   first (above).
4. **Check every entry** below that the pick meets: correct any it made
   wrong, and say in the entry what of the pick was left out for it.
5. **Build and test** as docs/build-and-test-reference.md says. The test
   checklist covers the picked changes, as well as the usual regression
   spots.
6. **Propose it,** with the full diff as a file. Its message is upstream's,
   with the line `-x` adds. Where something was fitted or left out, a note
   below it says what, and which entries changed.
7. **Commit it once approved,** with upstream's author, then pick the
   next. With a note, the whole message goes in a file, given with `-F`
   in place of `--no-edit`:

   ```sh
   git commit --no-edit --author="$(git log -1 --format='%an <%ae>' <commit>)"
   ```

The next release's changelog credits each picked change to its author
(docs/versioning.md).

## Changing upstream files

An upstream file is one the `upstream` branch has, wherever it is:
port/main.c and port/recomp_manual.c are upstream files, port/freecam.c is
ours. JSRF-PC changes them where that serves its targets. Each change is
checked again whenever a picked commit meets it, so it is kept small and
visible:

- **The right place first.** Code goes where it belongs in the code's
  structure, even when that is an upstream file: a kernel fix goes in the
  kernel, never in `port/` only to avoid it.
- **Then the least invasive way there:** a file of our own (as
  `port/freecam.c` or `src/menu/`), a setting, or an existing hook, with
  the upstream file getting only the call into it.
- **Game function overrides** follow the project's rule on them (2.A.6).
  How one is made: port/recomp_manual.c defines the game function under its
  own name (as `sub_000A6100`), and the recompiler, given
  `--exclude-manual port/recomp_manual.c`, leaves that function to it. A
  hook calls the game's own code from there, which the recompiler emits as
  `sub_000A6100_gen`. What the hook adds goes in a file of our own where it
  can (as port/freecam.c). recomp_manual.c is upstream's, so each override
  needs an entry below.
- **Listed in the same commit.** A commit that edits or removes an
  upstream file, a `port/` override in recomp_manual.c included, adds or
  updates its entry below. Its proposal says which upstream files it
  touches and how that affects later picks. Adding a file of our own, or
  picking a commit from upstream, needs no entry.
- **Could suit upstream:** an entry says so when upstream could take the
  change too. Offering it upstream is the maintainer's call.

## Where we differ from upstream

Checked against upstream 0.5.6 (`4d31983`), the last release merged into
`main`. Every upstream file `main` has changed (M) or removed (D) since
then is named in an entry below, unless only a picked commit changed it:

```sh
git diff --name-status --diff-filter=MD 4d31983 main
git log --oneline --grep='cherry picked from commit' 4d31983..main
```

### The name JSRF-PC

- **Files:** port/CMakeLists.txt, port/main.c, port/xbe_check.c and .h,
  src/nv2a_gl/nv2a_gl.c (the window's title, the settings file's folder),
  CMakeLists.txt (a comment), and the launchers: jsrf-unleashed and
  package.sh in launcher/linux/, and jsrf_launcher.c, package.sh,
  resources.rc, engine.manifest, and launcher.manifest in launcher/windows/.
- **Why:** the program, its packages, window, messages, and folders are
  named JSRF-PC (jsrf-pc). The Linux launcher keeps its upstream file name
  in the repository, so upstream's changes to it still apply, and is
  packaged as jsrf-pc.
- **When picking:** keep our names, and give anything upstream adds that
  shows a name ours.

### Version from our tags

- **Files:** port/CMakeLists.txt, launcher/windows/package.sh.
- **Why:** the version comes from our tags (b1, b1.1, 1.0), never
  upstream's v-tags, and a beta's Windows file version is 0.0.N.M
  (docs/versioning.md).
- **When picking:** keep ours.

### README and the packages' README.txt

- **Files:** README.md, launcher/linux/README.txt,
  launcher/windows/README.txt.
- **Why:** they are our own: Linux and Windows only, our downloads, the
  in-game menu, the keys, and portable mode.
- **When picking:** keep ours. Read upstream's changes for anything players
  or builders need, and write it into ours (build steps go in
  docs/building-on-linux.md).

### .gitignore

- **Files:** .gitignore.
- **Why:** game files in upper or lower case, Media/, UDATA/, TDATA/,
  portable/, partition images, logs, and dumps can't be committed, nor
  .venv/ or BRINGUP.md.
- **When picking:** keep ours, and add upstream's new lines.

### In-game menu

- **Files:** CMakeLists.txt, port/CMakeLists.txt (C++, and libstdc++
  linked in statically on Linux), src/nv2a_gl/CMakeLists.txt
  (`NV2A_GL_MENU`), src/nv2a_gl/nv2a_gl.c (the menu over the window, its
  Display and Graphics rows, and `nv_window_restart` for File > Restart),
  src/input/xinput_device.c (the menu has the pad while it is open, and a
  pad's Back opens it), CONTRIBUTING.md, NOTICE, LICENSES/README.md,
  launcher/linux/package.sh and launcher/windows/package.sh (Dear ImGui's
  licence), tools/linux/install_deps.sh (a C++ compiler).
- **Why:** one menu (F1, or Back) for every setting, usable with a
  controller, in a window or under gamescope. It replaces upstream's Video
  menu bar on Windows and its anti-aliasing box on F8 (both removed from
  nv2a_gl.c), whose settings it has; F8 is the free camera's. It is built
  only for the SDL window: Android, the Mac, and the iPhone keep their own
  menus. Our own files: src/menu/, third_party/imgui/.
- **When picking:** check upstream's changes to the SDL window, keys and pad
  input against the menu, give a setting upstream adds to its Video menu or
  keys a Display or Graphics row, and run tests/menu.

### Graphics: the anisotropic filtering setting

- **Files:** src/nv2a_gl/nv2a_gl.c (`tex_aniso` takes the level in use, set
  by `aniso_start` and `aniso_take`, and the Graphics row), nv2a_gfx_gl.c
  and nv2a_backend.h (`aniso_max`, a member added to `Nv2aGfx`).
- **Why:** upstream gives every texture with mipmaps 16x anisotropic
  filtering, or `RECOMP_GL_ANISO`'s level. Ours is also a setting,
  Graphics > Anisotropic filtering, Off to 16x (off by default, where
  upstream has 16x), kept as aniso= and changed while the game runs: upstream's
  texture cache tells textures apart by how they are sampled, so a new
  level drops the textures it reaches, which are made again when next drawn.
  `RECOMP_GL_ANISO` still wins over the setting. MSAA, which upstream took
  from this fork in 0.5.5, and SMAA, which took the place of our FXAA, are
  upstream's, with rows in the in-game menu.
- **When picking:** the `Nv2aGfx` tables are filled in by position, and our
  members come last: a member upstream adds goes before `aniso_max`, in the
  same order in the struct and in nv2a_gfx_gl.c's table. Metal's table
  leaves ours out (NULL). Our default stays `ANISO_NONE`, off, whatever
  upstream's is. Run tests/gl_backend and tests/gl_aa.

### Shader cache

- **Files:** src/nv2a_gl/nv2a_gl.c, nv2a_gfx_gl.c, nv2a_backend.h,
  nv2a_shader_cache.c and .h, and comments in nv2a_psh.h and nv2a_vsh.h.
- **Why:** upstream's disk shader cache, with a setting (on by default, as
  upstream has it). Every program it keeps is built again at each start,
  from its sources and from the NV2A state it came from, so none is built
  while the game draws, even after a driver update or a translator change.
  Old drivers' binary files are removed, and Developer > Purge shaders
  deletes the cache.
- **When picking:** a change upstream makes to `Nv2aVshFixed` or
  `Nv2aPshState` changes what the cache keeps: raise `SC_LAYOUT` in
  nv2a_gl.c, and note it in CHANGELOG-JSRF-PC.md. Run tests/shader_cache
  and tests/gl_backend.

### Renderer fixes: depth bias, texture timing, MSAA depth

- **Files:** src/nv2a_gl/nv2a_gl.c, nv2a_gfx_gl.c, nv2a_backend.h (three
  members added to `Nv2aRenderState`: `poly_offset`, `poly_factor`, and
  `poly_units`; and `NV2A_U_POLYOFF`), nv2a_psh.c and nv2a_uniforms.c.
- **Why:** renderer bugs the audit for b1.1 found, fixed in the renderer:
  - **Depth bias:** the polygon offset methods (0x0338, 0x0384, 0x0388)
    are decoded and applied, and the W-buffer shader adds the same offset
    (`polyOffset`). Upstream ignores them.
  - **Texture creation timed,** decoding and handing to the device apart
    (`g_tmake`, an end-of-run line), where upstream reads the clock at
    every lookup, a cache hit included.
  - **No idle single-sampled depth** beside a multisampled target
    (`rb_need`, `rb_idle`): at 3x scale that is 10.5 MB a target, about
    40 MB in a level on the Ally X, held for nothing. The end-of-run
    report lists what each target holds.

  Could suit upstream. The game's mip levels and a texture per sampling,
  which this entry also had before 0.5.5, are upstream's now.
- **When picking:** `NV2A_U_POLYOFF` is the last uniform: keep it after
  upstream's, in the same place in nv2a_uniforms.c's table. None of this
  changes `Nv2aVshFixed` or `Nv2aPshState`, so `SC_LAYOUT` stays. Run
  tests/gl_backend (test_polyoffset.c, test_msaa.c) and tests/shader_cache.

### Draw profile by step

- **Files:** src/nv2a_gl/nv2a_gl.c (`PROF_STEP`, and a `[GLPROF]` line).
- **Why:** with `RECOMP_GL_PROFILE=1`, every 16th draw is also timed step
  by step, and the log says where a draw's time goes: setup, vertices,
  uniforms, textures, state, probes, glDraw, and after. The other draws
  read no more clocks than before, so the profile's other figures stay
  comparable. Could suit upstream.
- **When picking:** keep the seven `PROF_STEP` lines at the ends of their
  steps in `do_draw_body` when upstream moves code around them.

### The always-on probes' clock

- **Files:** src/nv2a_gl/nv2a_gl.c (`g_probe_now`).
- **Why:** the probes that print every few seconds with no switch
  (`[SURFACE]`, `[MOVERPOS]`, `[SINTAB]`, `[PIPE]`, the mover-box budget,
  and `[TIMELINE]`) read a clock taken once a pass, in `present`, where
  upstream's read one each in every draw. They print as before. Could
  suit upstream.
- **When picking:** an upstream probe of the same kind that reads the clock
  in `do_draw_body` can use `g_probe_now` too.

### Reversed and precise depth

- **Files:** src/nv2a_gl/nv2a_gl.c (`revz_start`, `ff_revz_row`, and
  `depth_dir` for the diagnostics' depth state), nv2a_gfx_gl.c (`g_revz`
  and `depth_dir`: the depth comparisons, clear, polygon offset, and
  readback), nv2a_vsh.c and .h (`nv2a_glsl_set_revz` and
  `nv2a_glsl_set_precz`, and the depth in the epilogues), nv2a_psh.c (the
  W-buffer depth), nv2a_backend.h and nv2a_uniforms.c (`NV2A_U_FFREVZ`,
  `ffRevZ`).
- **Why:** near the far end of the 24-bit range a float holds only whole
  depth steps, and the title layers its menus and decals a step or two
  apart there. Upstream's depth, taken to -1..1 and back, and its
  fixed-function z, summed at about three billion before the divide, moved
  by a step or more there, so distant objects flickered and the Graffiti
  menu's icons tied. Clip control gives OpenGL the console's 0..w range,
  the emitters count depth down from the far end, where floats are finest,
  and the fixed-function depth row is worked out in double once a draw.
  Desktop OpenGL only, with 4.5 or ARB_clip_control, on by default;
  `RECOMP_GL_NOREVZ` and `RECOMP_GL_NOPRECZ` turn them off. With reversed
  depth off, and on OpenGL ES and Metal, the shaders are as they were
  without it. Could suit upstream.
- **When picking:** keep `NV2A_U_FFREVZ` the last of the vertex uniforms,
  after upstream's, in the same place in nv2a_uniforms.c's table. A
  glDepthFunc, glClearDepth, or glPolygonOffset upstream adds turns round
  with `depth_dir` and `g_revz`, and a new depth in an epilogue needs its
  reversed form beside it. None of this changes `Nv2aVshFixed` or
  `Nv2aPshState`, so `SC_LAYOUT` stays. Run tests/gl_backend (test_revz.c)
  and tests/shader_cache.

### GL errors once a frame

- **Files:** src/nv2a_gl/nv2a_gl.c (`gl_error_check`, and the switch on
  `vc_debug_switch`'s list).
- **Why:** upstream asks `glGetError` after every draw. With Mesa's GL
  thread, on by default for radeonsi, the call waits for the driver's
  thread to run everything queued before it, so every draw waited for the
  driver: on the Ally X a draw took 7.8 us with it and 5.1 without. It is
  asked once a frame, in `present` before frame pacing, and says the frame
  and the draw count. `RECOMP_GL_ERRORS_PER_DRAW=1` asks after every draw,
  as upstream does, to name the draw; it reads nothing back, so it leaves
  the vertex cache on. Could suit upstream.
- **When picking:** an upstream error check in `do_draw_body` goes through
  `gl_error_check` too.

### The walk by kind of method

- **Files:** src/nv2a_gl/nv2a_gl.c (`pb_kind`, `pb_kind_method`, and a
  `[GLPROF]` line).
- **Why:** with `RECOMP_GL_PROFILE=1`, the pushbuffer walk outside the
  draws is broken down by kind of method (inline vertices, vertex
  attributes, indices, constants, matrices, vertex arrays, begin/end,
  vertex programs, textures, surfaces and clears, flips and fences, other
  state), with what is left as the walk itself. Each kind is counted, and
  one call of each in 61 is timed net of the draws and presents it sets
  off. The clock reads that timing takes are shown on their own, not as
  part of the walk: timing every call of the rarer kinds took a quarter
  to a third of the renderer thread's time in Shibuya Terminal's crowd.
  With the crowd's mix of methods, the profile adds about 2.5 ns a word
  now, against 18 before. Upstream's `nv2a_gl_method` is `gl_method`
  here, behind a wrapper that only counts with the profile on. Could suit
  upstream.
- **When picking:** upstream's changes to `nv2a_gl_method`'s body go into
  `gl_method`.

### The driver's nudge off both paths

- **Files:** src/nv2a_gl/nv2a_vsh.c and .h (`NV2A_D3D_NUDGE`,
  `nv2a_vsh_ff_vpoff`, and `nv2a_vsh_vprect`, and the comment on the
  fixed-function shader's `ffVpOff`), nv2a_gl.c (the `ffVpOff` and `vpRect`
  uploads in `do_draw_body`, which worked `vpRect` out inline).
- **Why:** the console's D3D driver adds 0.53125 of a pixel to every
  position it hands the hardware. Upstream took it off the vertex programs'
  viewport (`vpRect`) but not the fixed-function offset (`ffVpOff`), so every
  vertex-program draw landed half a pixel up and left of the fixed-function
  world. On a wall seen at an angle that is many depth steps: the garage's
  graffiti tiles, drawn by a program onto fixed-function walls at the wall's
  own depth, flickered and fell behind them. Both are now worked out in
  nv2a_vsh.c, both less the nudge. It comes off rather than staying on so
  that the title's whole pixels are pixel edges at every internal
  resolution: kept on both, as xemu does, it moves the title's 2D too, and
  a letterbox bar drawn from the corner leaves a pixel and a half bare at
  3x. Could suit upstream.
- **When picking:** upstream changes to the `vpRect` block or the `ffVpOff`
  upload in `do_draw_body` go into those two functions. `Nv2aVshFixed` is
  unchanged, so `SC_LAYOUT` stays. Run tests/gl_backend (test_nudge.c) and
  tests/shader_cache.

### Per-vertex methods told apart first

- **Files:** src/nv2a_gl/nv2a_gl.c (`attr_method`, with `M_SET_VERTEX3F`
  and `M_SET_VERTEX_DATA2F_M`, and the first test in `imm_method`).
- **Why:** upstream's `imm_method`, new in 0.5.6, tests a method against
  some 40 ranges in turn before it says the method isn't a per-vertex
  attribute, and every method `gl_method` hasn't dealt with by then reaches
  it: the index words among them, about 8 million a second on 99th Street.
  `attr_method` tests the two ranges the per-vertex methods lie in first, so
  the rest leave at once. In the sandbox an index word took 11 to 20 ns in
  `imm_method`, and takes about 2 ns with it. `pb_kind`'s vertex attributes
  are the same two ranges and use it too. Could suit upstream.
- **When picking:** a per-vertex method upstream adds to `imm_method` must
  lie in `attr_method`'s ranges. Run tests/attr_method, which fails if one
  doesn't.

### Targets sampled without alpha

- **Files:** src/nv2a_gl/nv2a_gl.c (`fmt_opaque`, and its call where each
  stage's texture is bound in `do_draw_body`), nv2a_gfx_gl.c and
  nv2a_backend.h (`texture_opaque`, a member added to `Nv2aGfx`).
- **Why:** the jet boost renders the world into a 512x512 and a 128x128
  target, both R5G6B5, which has no alpha bits, and lays the 512x512 one
  over the frame blended by the alpha it samples there, as
  LU_IMAGE_R5G6B5. The console reads that alpha as 1. Our targets are
  RGBA8 and keep the alpha every draw into them wrote, so the copy let the
  frame under it show through in hard-edged patches: a dark hexagon on the
  ground around the skater, and ghosting. A target sampled as a format with
  no alpha bits (R5G6B5, X1R5G5B5, X8R8G8B8, Y8) now reads alpha 1, as
  `decode_texel` gives it from memory, by a texture swizzle. Metal leaves
  `texture_opaque` NULL and keeps the bug. Could suit upstream.
- **When picking:** keep `texture_opaque` after our other `Nv2aGfx` members,
  and the call right after the stage's texture is bound: the swizzle
  belongs to the texture, so it is set on every draw that samples a target.
  Run tests/gl_backend (test_opaque.c).

### Frame pacing from the first frame

- **Files:** src/nv2a_gl/nv2a_gl.c (`frame_pace`).
- **Why:** upstream leaves the frame rate uncapped until the first frame
  with 100 draws or more, so that the cap doesn't hold loading back. The
  opening cards come before it, and the game times them in frames, so they
  went by in under a second each. Paced from the first frame, they play as
  on the console: about 6 seconds a logo, and at least 7 for the
  disclaimer. The pacing's wait gives up the guest lock, so the game's
  other threads run on through it. Could suit upstream.
- **When picking:** keep ours, with no exception for the boot in
  `frame_pace`.

### Full screen remembered

- **Files:** src/nv2a_gl/nv2a_gl.c.
- **Why:** full screen is kept in the settings file, as widescreen and the
  scale are, so the game starts the way it was left. Upstream starts in a
  window unless the launcher is given `--fullscreen`. Could suit upstream.
- **When picking:** keep ours.

### Restart

- **Files:** port/main.c (`recomp_restart_self`),
  src/nv2a_gl/nv2a_shader_cache.c.
- **Why:** a restart (a new render scale, Purge shaders) passes the new
  run only the standard handles. Upstream's passes on every open file, and
  on Linux the new run kept the last run's guest memory until the game
  closed. Could suit upstream.
- **When picking:** keep ours, and run tests/restart.

### Free camera

- **Files:** port/recomp_manual.c (hooks in `sub_00116E30`,
  `sub_001C3E14`, `sub_000A5070`, and `sub_000A6100`, and the names of the
  camera fields and addresses they use, at the top of the camera section),
  port/main.c and port/CMakeLists.txt (starting and building it),
  src/nv2a_gl/nv2a_gl.c (`nv_window_hotkey`, keys a title binds for itself:
  F8, in place of upstream's anti-aliasing box).
- **Why:** the right stick turns the camera around the player. While it
  does, the camera doesn't turn to graffiti being sprayed or to whoever the
  player talks to, and its eye isn't lifted, since the game lifts it for
  where its own camera faces. Our own files: port/freecam.c and freecam.h.
- **When picking:** fit upstream's changes to those four functions around
  the hooks, and run tests/freecam. The names are only for our hooks:
  upstream's own lines in the camera section keep their numbers.

### Skip intro

- **Files:** port/recomp_manual.c (a hook in `sub_0007E360`, the intro's
  update, and the names of the fields and states it uses), port/main.c and
  port/CMakeLists.txt (starting and building it).
- **Why:** Display > Skip intro (`skipintro=1`) leaves out the logos and the
  graffiti disclaimer: their task ends before its first update, and the game
  opens at the title screen's loading. Off by default, so the intro plays as
  on the Xbox. The hook also logs each card's steps, as `[INTRO]` lines. Our
  own files: port/intro.c and intro.h.
- **When picking:** fit upstream's changes to `sub_0007E360` around the
  hook.

### Developer menu

- **Files:** port/main.c, port/CMakeLists.txt, src/nv2a_gl/nv2a_gl.c
  (`nv_window_file_get` and `_set`: a number kept in a file beside the
  settings file, which port/developer.c uses for developer.cfg; and in
  `vc_debug_switch`, six switches that leave the vertex cache on:
  TITLE_MIPS, TEX_ONESAMPLER, TEXGEN, UNHANDLED, IMMEDIATE, and STENCILVIS).
- **Why:** the in-game menu's Developer menu: Log to file, the engine's
  logging switches, Engine switches, which turn parts of the engine off to
  compare (the GPU queue, below, among them), and Experimental, switches
  that change what the game does, for a comparison, shown while there is
  one. The start puts them into the environment before anything reads
  them. Among the logging switches is the CPU profile (Linux only), which
  the start sets going: where each thread's time goes, by function.
  Upstream's `vc_debug_switch` list leaves out the six above, none of which
  reads a draw's vertices back, so setting one turned the vertex cache off.
  Our own files: port/developer.c and port/cpuprof.c, both built from
  port/CMakeLists.txt. Could suit upstream (the list).
- **When picking:** keep the two calls in port/main.c, the two files in
  port/CMakeLists.txt, and the six names in `vc_debug_switch`, and run
  tests/developer and tests/cpuprof.

### Portable mode

- **Files:** launcher/linux/jsrf-unleashed,
  launcher/windows/jsrf_launcher.c, src/kernel/kernel_path.c
  (`JSRF_USER_DIR`: the game's saves, E:\UDATA and E:\TDATA, in the
  portable folder). With that third folder, the path rules name the folder
  each one goes to, `PATH_GAME_DIR`, `PATH_SAVE_DIR`, or `PATH_USER_DIR`,
  where upstream writes 0 or 1.
- **Why:** a folder named `portable` beside the launcher keeps the
  settings, saves, logs, and cache, so the whole game can move, as to an SD
  card (docs/portable-mode.md).
- **When picking:** keep ours, give a path rule upstream adds or changes the
  name for its 0 or 1, and run tests/user_dir.

### The Linux launcher's help

- **Files:** launcher/linux/jsrf-unleashed.
- **Why:** `--help` prints the header comment, up to the first line that
  isn't a comment. Upstream's prints a fixed range of lines (4 to 18), so a
  header that changes length cuts the help short or prints code, as ours
  did. Could suit upstream.
- **When picking:** keep ours.

### Fades to black

- **Files:** launcher/linux/jsrf-unleashed, launcher/windows/jsrf_launcher.c
  (`RECOMP_GL_CURTAIN` left out of the environment they set).
- **Why:** upstream's launchers set `RECOMP_GL_CURTAIN=skip`, which leaves
  out the full-screen black quad the game fades with. It was a workaround:
  the tutorial once stayed black under that quad, with the HUD over it. Its
  cost was that every fade cut instead, the intro's cards, the title, and
  loading into a level among them. In testing on Linux and Windows the
  tutorial fades in and stays clear, and every fade seen ends, so the game
  draws its fades as on the Xbox. Developer > Engine switches >
  RECOMP_GL_CURTAIN=skip brings the workaround back from the next start, as
  does the variable set before the launcher starts, and `log` reports the
  fade's alpha once a second. The Mac, iPhone, and Android launchers keep
  upstream's line, since they weren't tested.
- **When picking:** keep ours, without the variable, and take upstream's
  other changes to those lines.

### Kernel fixes

- **Files:** src/kernel/kernel_bridge.c, kernel.h, kernel_hal.c,
  kernel_thunks.c, kernel_rtl.c, xbox_memory_layout.c and .h, and
  port/recomp_manual.c.
- **Why:** seven fixes, each of which could suit upstream:
  - **NtProtectVirtualMemory and KeSetDisableBoostThread** (ordinals 204
    and 144) are bridged (kernel_bridge.c). JSRF calls both, and upstream
    leaves them to the fallback, which never writes the old protection.
    tools/kernel_audit/test_bridge_bodies.py (ours) tests them.
  - **HalRequestSoftwareInterrupt and HalClearSoftwareInterrupt are
    fastcall,** with the level in ecx and nothing to pop, as on the
    console (kernel.h, kernel_hal.c, kernel_bridge.c). Upstream reads the
    level from the stack and pops 4 bytes.
  - **A second release of VirtualAlloc memory fails,** as on the console
    (kernel_bridge.c, xbox_memory_layout.c and .h). JSRF's graffiti cache
    releases the same buffer twice on every scene change, and Windows
    can't release part of the guest memory.
  - **Quieter logs:** a line for every event and file read only with
    `RECOMP_KERNEL_IO=1` (kernel_bridge.c; the event lines were written
    with the guest lock held), and the thunk table's unresolved ordinals
    at debug level (kernel_thunks.c).
  - **printf formats:** `GetCurrentThreadId()` and `GetLastError()` cast
    to unsigned long for `%lu`, as DWORD is 32 bits on Linux (kernel_rtl.c,
    xbox_memory_layout.c).
  - **A display interrupt acknowledged inside a DPC is completed at a loop
    back edge** (kernel_bridge.c), as the hardware completes it at the
    write. The title's vblank DPC writes its acknowledgement and spins,
    holding the guest lock, until PMC_INTR_0 is quiet. Upstream completes
    it only when the GPU thread next looks (`vblank_intr_tick`).
    tests/vblank_ack (ours) tests it.
  - **A software method's interrupt bits are cleared only by the title's
    handler** (kernel_bridge.c, xbox_memory_layout.c, and `sub_00194210`
    in port/recomp_manual.c). The GPU thread cleared PGRAPH_INTR and
    PMC_INTR_0 whole whenever no software method was in flight, and wiped
    one raised between its check and its clear: D3D's handler never saw
    it, and the game slept for good in its wait for the GPU (0x0019151B).
    Now the GPU thread leaves those two bits alone while software methods
    are delivered (`xbox_nv2a_swm_on`), and every change to the two
    registers is one atomic operation. Rare while each flush waited for the
    GPU thread; once in 13,228 wake-ups in a run with the queue (below).
    tests/nv2a_queue (ours) tests it: the old code loses a wake-up within a
    few thousand, this none in fourteen million.
- **When picking:** keep each fix. Where a picked commit fixes the same
  thing, the maintainer chooses which fix stays; if it is upstream's, drop
  ours from this entry.

### The game running ahead of the GPU thread

- **Files:** src/kernel/xbox_memory_layout.c and .h
  (`xbox_Nv2aRegisterWritten`, `nv2a_queue_on`, `nv2a_queue_report`, and
  where `nv2a_ack_thread` sets DMA_GET and walks the ring's tail),
  src/kernel/nv2a_pb_scan.c (`nv2a_pb_scan_keep_get`: the walk moves
  DMA_GET, and the barrier), src/kernel/kernel_mmio.c (the call into
  xbox_memory_layout.c), templates/runtime/recomp_types.h
  (`RECOMP_MMIO_LO` takes in the NV2A).
- **Why:** D3D's KickOff asks for a write-combine flush and spins until
  the GPU thread clears it, and with the pushbuffer executor that thread
  is the renderer: it clears it only between walks, and not while frame
  pacing sleeps at a flip. So the game was never more than one kick ahead
  of the renderer, and a frame cost the two one after the other. In
  Shibuya Terminal's crowd on the Ally X the game's thread spent 43-67% of
  its time in that spin. Now the flush is complete at the write, as xemu
  answers it, and DMA_GET says how far the walk has read instead of being
  set to DMA_PUT before it, so D3D's own waits on GET and on the fences hold
  the game back, as on the console. D3D's wait for the GPU turns a NOP ahead
  of GET into NOP(5), flushes, reads GET again, and sleeps until the GPU
  reaches that NOP. So GET moves with each command the walk reaches, and is
  published before the command is read with a full barrier between them, as
  the flush is one on the game's side. With GET a whole walk behind, or with
  either barrier missing (x86 lets a read overtake an earlier write), the
  walk could read the NOP before the patch while D3D still saw GET short of
  it, and the game slept for good. When the ring wraps, the walk reads its
  tail to the jump however far that is: it read at most 128 KB, enough while
  each flush waited for the walk, but the game can now be most of the ring
  ahead, and the fence D3D slept on was in the part left unread. On the Ally
  X in Shibuya Terminal, with the sound fix below, the game's thread went
  from 96-97% of a core to 27-32%, and frames that missed a refresh from
  0.5% to 0.1%. On since b1.2 (an experiment in b1.1); `RECOMP_NV2A_QUEUE=0`
  (Developer > Engine switches) gives the old way, to compare.
  The generated code's recomp_types.h has to be the new one, and the
  pipeline never overwrites it: delete it and generate it again. With an
  old one the flush waits as before, and the queue's report says so.
  tests/nv2a_queue (ours) tests it, D3D's wait for the GPU included: twenty
  thousand waits against the real walk without a wake-up lost, and a wrap
  with 499 KB of tail, all of it walked. Could suit upstream.
- **When picking:** keep ours. If upstream changes how the GPU thread sets
  DMA_GET or the device window's bounds, fit ours around it, and run
  tests/nv2a_queue.

### The sound chip a frame at a time

- **Files:** src/apu/apu_core.c (`throttle`, `ring_ahead`).
- **Why:** the APU's frame thread made the voice processor's frames eight
  at a time on the wall clock, and with the output ring sixteen at each
  pull of the device, every 10.7 ms with SDL. DirectSound waits for the
  voice processor whenever it restarts a sound: it turns the voice off and
  spins until a frame has found it idle and the APU interrupt handler has
  retired it. Each restart waited 5 ms or so, against under one on the
  console (a frame is 32 samples, two thirds of a millisecond), and where
  JSRF restarts one sound two to four times a frame, in Shibuya Terminal,
  the game fell to 30 fps. Now each frame is paced on its own: with the
  ring, once the device has played enough of its last pull to want it; on
  the wall clock, at its eighth of the encode frame. tests/apu_pacing
  (ours) tests both: 0.6 ms on average to the next frame, against 5.3 with
  the ring and 2.9 on the wall clock before. In Shibuya Terminal on the
  Ally X, frames that missed a refresh went from 1.6-1.7% to 0.5%. Could
  suit upstream.
- **When picking:** keep ours. If upstream changes the frame thread's
  pacing, make it pace single frames as here and run tests/apu_pacing.

### Diagnostics only in Debug builds

- **Files:** port/CMakeLists.txt.
- **Why:** `RECOMP_ABI_CHECK` and `RECOMP_ICALL_FEEDBACK` put code at every
  call site of the generated code, whether used or not. Release builds
  leave them out (the options `JSRF_ABI_CHECK` and `JSRF_ICALL_FEEDBACK`,
  on in Debug builds): 36.2 MB of code instead of 46.0 MB.
- **When picking:** keep ours.

### Code for upstream's older generated code, left out

- **Files:** port/recomp_manual.c (the seeded and holes tables' lookups,
  and the override of DirectSound's stop-pending wait),
  port/recomp_tables_none.c (removed) and its block in
  port/CMakeLists.txt, src/kernel/kernel_bridge.c
  (`recomp_guest_spin_yield`, its counters, and comments about them).
- **Why:** we build only from code generated with today's tools
  (docs/building-on-linux.md), which needs none of it. Upstream keeps it
  for its downloads, which it still builds from older generated code. The
  merge of upstream 0.5.4 (`ce20630`) says what was kept out.
- **When picking:** leave upstream's changes to this code out, and say so
  in the commit. When upstream removes it too, remove this entry.

### Unused renderers and test code, removed

- **Files:** src/d3d/d3d8_gl.c (removed), src/d3d/CMakeLists.txt and
  d3d8_internal.h; src/nv2a/nv2a_pgraph_d3d11.c and .h, nv2a_pb_replay.c,
  nv2a_pb_test.c, menu_pb_*.h, menu_pushbuffer_data.h, and
  font_atlas_data.h (removed), src/nv2a/CMakeLists.txt and nv2a_core.c;
  and what named them: src/nv2a/README.md, docs/technical/nv2a-shaders.md,
  src/kernel/nv2a_pb_exec.c, src/kernel/recomp_trace.c,
  src/platform/win32_compat.c, and tools/linux/install_deps.sh (libepoxy,
  which on Linux only d3d8_gl.c needed).
- **Why:** not recorded. They were already gone from the fork's first
  commit (`a7fcd0e`), and the merge of upstream 0.5.0 (`ae7f1ad`) kept them
  out. None of them runs in JSRF, which draws through src/nv2a_gl.
- **When picking:** an upstream change to a removed file is a modify/delete
  conflict: keep the file removed.
- **To decide:** restoring them would end these conflicts, at the cost of
  building code that never runs, and of libepoxy on Linux.

### Leftover differences

- **Files:** port/recomp_manual.c, src/kernel/kernel_bridge.c,
  tools/kernel_audit/test_bridge_ordinals.py.
- **Why:** none. Where upstream took our code and changed its comments,
  credited spriteice's fork, or declared a function twice, the merges kept
  our text. The test lacks upstream's allowance for ordinal 150, which both
  route the same way, and finds its function by a longer pattern.
- **When picking:** keep ours.

### Our own files

- **Files:** every file upstream doesn't have, which
  `git diff --name-only --diff-filter=A upstream main` lists: among them
  port/freecam.c and port/developer.c, src/menu/, third_party/imgui/ (Dear
  ImGui, unmodified), our tests (in tests/, and
  tools/kernel_audit/test_bridge_bodies.py), CHANGELOG-JSRF-PC.md, and our
  docs in docs/.
- **Why:** they have no counterpart upstream, so they never conflict, and
  adding one needs no entry. They use upstream's code.
- **When picking:** build them and run their tests. If upstream adds a file
  at one of these paths, stop and ask.
