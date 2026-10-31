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
   checklist covers the areas the picked changes touch.
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
  (`[SURFACE]`, `[MOVERPOS]`, `[SINTAB]`, `[PIPE]` and the mover-box
  budget, and `[TIMELINE]` when its switch is on, below) read a clock
  taken once a pass, in `present`, where upstream's read one each in every
  draw. They print as before. Could suit upstream.
- **When picking:** an upstream probe of the same kind that reads the clock
  in `do_draw_body` can use `g_probe_now` too.

### The timeline probe off by default

- **Files:** src/nv2a_gl/nv2a_gl.c (the `[TIMELINE]` probe in
  `do_draw_body`, and `TIMELINE` on `vc_debug_switch`'s list).
- **Why:** upstream's probe printed one frame every 5 s with no switch: each
  surface the frame drew into, each skinned batch and each small quad, up to
  200 lines, from the GPU thread, then flushed the log. In the b1.2 review's
  runs on the Ally X it was nearly half of every log's lines, and slow
  frames of 27 to 38 ms often came right after it, though not always: in a
  run with it switched back on, none of its 47 frames was followed by one.
  It asks one question, whether the characters are drawn after the frame's
  composite, so it is now off unless `RECOMP_GL_TIMELINE=1` (Developer >
  Logging) asks for it, and prints as before when it does. It reads nothing
  back, so it is on `vc_debug_switch`'s list and leaves the vertex cache on.
  tests/timeline (ours) tests it. Could suit upstream.
- **When picking:** if upstream changes the probe, keep `tl_on` in its
  arming test, and `TIMELINE` on `vc_debug_switch`'s list. Run
  tests/timeline and tests/developer.

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

### The graphics card's own clock

- **Files:** src/nv2a_gl/nv2a_gl.c (`card_mark`, `card_report`, and a
  `[GLPROF]` line).
- **Why:** with `RECOMP_GL_PROFILE=1`, each frame ends with a timestamp
  query, and the log says once a second how long after its hand-over the
  graphics card finished each frame, and how far apart it finished them by
  its own clock. The profile's other figures time the renderer's thread,
  and cannot say whether the card keeps up. The answers are read once a
  second, none waited for, as each read waits for Mesa's GL thread.
  OpenGL's timer queries (3.3); OpenGL ES goes without. tests/gl_card
  (ours) tests it. Could suit upstream.
- **When picking:** keep the `card_mark` call where `present` shows the
  frame, before the window's swap, and the `card_report` call in
  `prof_report`.

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

### Index words a run at a time

- **Files:** src/kernel/nv2a_pb_scan.c (the walk hands a run of index words
  to `nv2a_pb_exec_indices`, and the index methods' names),
  src/kernel/nv2a_pb_exec.c (`nv2a_pb_exec_indices`), src/nv2a_gl/nv2a_gl.c
  and .h (`nv2a_gl_indices` and `gl_run`, and `pb_kind_method` takes a run).
- **Why:** more than half the words the title sends are indices, about 7
  million a second on 99th Street: ARRAY_ELEMENT16 or 32, one method written
  non-incrementing, up to 2047 words at a time. Each went through the walk,
  the executor and `gl_method` on its own to land in a list. Now the walk
  hands the executor a run whole, and the renderer puts its first word
  through `gl_method`, which starts the batch or draws what it holds first,
  and appends the rest in one go, dropping those past a full list as
  before. In the sandbox the renderer's part went from about 14 ns a word to
  under 2. Under `RECOMP_PB_SCAN` or `RECOMP_PB_SOFT`, and on a subchannel
  other than 0, the words go one at a time as before. With
  `RECOMP_GL_PROFILE=1` a run is counted by its words and timed as one call.
  tests/index_runs (ours) tests the walk and the renderer against the same
  words one at a time. Could suit upstream.
- **When picking:** keep ours. If upstream gives the index methods anything
  to do outside their cases in `gl_method`'s switch, or changes those cases,
  `gl_run` must do the same for every word of a run. Run tests/index_runs.

### The big-batch log's numbers named

- **Files:** src/nv2a_gl/nv2a_gl.c (`batch_size_note`, from upstream's
  9d21bd8, picked as 871a332).
- **Why:** upstream's log of batches past the old limit wrote that limit,
  16384, and how often it prints as bare numbers. They are named now:
  `OLD_BATCH_MAX`, `BIG_BATCH_SAID`, `BIG_BATCH_EVERY` and
  `CUT_BATCH_SAID`. The log says the same as before, word for word.
  Could suit upstream.
- **When picking:** if upstream changes `batch_size_note`, keep the names
  in place of the numbers. Upstream's 8f5723a adds code after the
  function and edits the line after its call, so it doesn't meet them.

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

### The pacing spin on Linux

- **Files:** src/nv2a_gl/nv2a_gl.c (`pace_margin`, `pace_margin_learn`,
  `pace_sleep_until`, their use in `frame_pace`, and the end of the
  `[PACE]` line).
- **Why:** `frame_pace` spins on the clock for the last 1.2 ms of every
  frame's wait. On the Ally X that spin was 14 to 37% of the GPU thread's
  samples, about 7% of a core, and Linux's sleeps seldom needed it. On
  Linux each sleep now goes to an absolute time, and the spin starts at
  0.3 ms. It widens at once when a sleep ends close to it, 1.2 ms at most,
  and narrows back by 2 us a frame. `RECOMP_PACE_SPIN_US` fixes it. The
  `[PACE]` line says how long the spin is and how far a sleep ran over.
  Windows and macOS are unchanged. tests/pace_spin (ours) tests it. Could
  suit upstream.
- **When picking:** keep the Linux branch in `frame_pace`'s wait, and
  upstream's own for the other systems.

### Frame pacing in step with the display on Linux

- **Files:** src/nv2a_gl/nv2a_gl.c (`gl_init` starts it, `frame_pace`
  paces by it and sends the frame to the card before waiting, and
  `nv2a_gl_report` reports it), src/nv2a_gl/CMakeLists.txt (builds it on
  Linux). The rest is in src/nv2a_gl/nv2a_display_clock.c and .h (ours).
- **Why:** on Linux the game paced on its own 60 Hz clock, which never
  quite keeps time with the display's. At 120 Hz the moment each frame was
  handed over drifted through the refresh, and where it passed the point at
  which the compositor takes a frame, frames stayed one refresh, then
  three. The kernel knows when the display refreshes (the X server under
  KWin and gamescope does not), so `frame_pace` now paces at a whole number
  of its refreshes and hands each frame over 15% into one, as the presenter
  does on Windows half a refresh in. It sends the frame's commands to the
  card before it waits, so the card draws the frame meanwhile. With VRR on,
  or where no whole number of refreshes is the game's frame (144 Hz), the
  game's clock paces, as before. `RECOMP_PACE_DISPLAY=0` (Developer >
  Performance > Display pacing, unticked) turns it off, and
  `RECOMP_PACE_PHASE` sets the point in the refresh. A line every 10 s says
  where in the refresh the frames were handed over. tests/display_clock
  (ours) tests it. Could suit upstream.
- **When picking:** keep the point `frame_pace` eases towards as `at`
  (half a refresh for the presenter), the call to `nv2a_dclock_lock` when
  `pres_lock` gives nothing, and the flush before the wait. Run
  tests/display_clock.

### Textures made faster

- **Files:** src/nv2a_gl/nv2a_gl.c (`tex_dxt_direct` and its use in
  `upload_texture_inner`; `decode_level`'s loops, `put_rgba`,
  `decode_rows` and `decode_texels`, with `decode_texel` always inlined;
  the texture cache's `held`; `DXT` on `vc_debug_switch`'s list; and the
  DXT counts at the end of the `[HITCH]` line and of the textures-made
  line), nv2a_gfx_gl.c and nv2a_backend.h (`tex_new_dxt`, a member added
  to `Nv2aGfx`).
- **Why:** entering a level makes 50 to 110 textures in one frame. On the
  Ally X those frames took 50 to 217 ms, 75 to 87% of it making textures,
  and 61 to 75% of that was decoding them. DXT1, 3 and 5 were decoded too,
  so that no texture would depend on the driver taking S3TC. They now go to
  the device as they are where it takes them (every desktop driver does).
  They're decoded as before where it doesn't, and where the texels are
  read: a bump source's bias, the transparency census and the texture
  dump. Every other format is decoded with a loop of its own, two to seven
  times as fast here and texel for texel the same. `RECOMP_GL_DXT=0`
  (Developer > Performance > Compressed textures direct, unticked) decodes
  DXT as before. Metal leaves `tex_new_dxt` NULL and decodes.
  tests/tex_decode and tests/gl_backend's test_dxt.c (ours) test it. Could
  suit upstream.
- **When picking:** keep `tex_new_dxt` last among our `Nv2aGfx` members, in
  the struct and in nv2a_gfx_gl.c's table. Fit a change upstream makes to
  `decode_level` or `decode_texel` into the loops, then run tests/tex_decode
  and tests/gl_backend.

### A wider content hash

- **Files:** src/nv2a_gl/nv2a_gl.c (`vc_hash` hands 256 bytes and more to
  `nv2a_hash_wide`). The rest is in src/nv2a_gl/nv2a_hash.h (ours).
- **Why:** `vc_hash` hashes every byte of what each kept draw's vertices
  are made from, and of the textures the once-a-frame check compares: in
  99th Street on the Ally X over 300 MB a second, 7% of the GPU thread's
  time. Its loop is xxHash64's, two 64-bit multiplies for every 8 bytes.
  On x86-64, 256 bytes and more now go to a wider hash after XXH3's
  approach: eight lanes of 32-by-32-bit multiplies, with SSE2, or with AVX2
  where the processor has it (on Linux and Windows, built with GCC or
  Clang). From 1 KB up it is about 1.7 times as fast with SSE2 and 2 to 3
  times with AVX2. Under 256 bytes, and on other processors, `vc_hash` is
  as it was. The hashes only live in memory, so nothing saved changes, and
  the texture check keeps its 1 MB a frame. tests/vc_hash (ours) tests it.
  Could suit upstream.
- **When picking:** keep the hand-over at the top of `vc_hash`, before its
  own loop. Run tests/vc_hash.

### Combiner constants as uniforms

- **Files:** src/nv2a_gl/nv2a_psh.c (`reg_name` names `pshC[]`, the
  constants' literals are gone, and the GLSL declares `pshC`), nv2a_gl.c
  (`psh_hash` and `sc_state_note` take `nv2a_psh_key`, `psh_consts_send`
  and its call after the fog colour in `do_draw_body`, and `psh_c` and
  `psh_ok` in `g_last`), nv2a_uniforms.c and nv2a_backend.h (`pshC`, a
  uniform added after `polyOffset`), nv2a_psh.h (a comment). The rest is in
  src/nv2a_gl/nv2a_psh_consts.h (ours).
- **Why:** each combiner stage's C0 and C1 and the final combiner's pair
  were written into the fragment shader as literals, and the program key
  hashed all of `Nv2aPshState`, so each new value was a new program. The
  game fades the spray can's effects and the sewer's splashes through them,
  and each shade was a program built on the GPU thread, about 8 ms each, up
  to 3 a frame: 22% of the frames late in a 10 s report on the Ally X, and
  413 programs more in the shader cache after one short run. The shader now
  reads them from one uniform array, sent when they change, and the key and
  the shader cache's states leave them out, with the alpha reference, which
  was a uniform already. The states the cache kept before make the same
  programs, so `SC_LAYOUT` stays. tests/psh_consts and tests/gl_backend's
  test_psh_consts.c (ours) test it. Could suit upstream.
- **When picking:** keep `pshC` last in the uniform table and the enum. An
  upstream change that reads another field of `Nv2aPshState` as a uniform
  can leave it out of the key in `nv2a_psh_key`; one that writes a field
  into the shader's source must keep it in. Run tests/psh_consts and
  tests/gl_backend.

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
  camera fields and addresses they use, at the top of the camera section;
  one call, `freecam_ran`, in upstream's camera counter on `sub_000A28B0`),
  port/main.c and port/CMakeLists.txt (starting and building it),
  src/nv2a_gl/nv2a_gl.c (`nv_window_hotkey`, keys a title binds for itself:
  F8, in place of upstream's anti-aliasing box; `nv_window_pad_hotkey`, a
  pad's buttons the same way, R3, run from `sdl_pump` on
  `SDL_CONTROLLERBUTTONDOWN` while the menu is shut).
- **Why:** the right stick turns the camera around the player. While it
  does, the camera doesn't turn to graffiti being sprayed or to whoever the
  player talks to, and its eye isn't lifted, since the game lifts it for
  where its own camera faces. It steers only a camera whose skater a pad
  moves (`cam_pad_steered`: the player manager's pad and control, the
  game's own test), so a scene shown through another skater's camera stays
  the game's. A camera the game doesn't post-update (`sub_000A28B0`), as
  while paused, keeps the free camera's view. `freecam_walls` keeps the
  corners of the view's near plane clear of walls, as well as the line to
  the eye, sized from the camera device's projection: the eye comes in at
  once and goes back out by degrees (`freecam_wall`), so a wall going by
  doesn't make the view jump. Our own files: port/freecam.c and
  freecam.h.
- **When picking:** fit upstream's changes to those four functions, and to
  the counter on `sub_000A28B0`, around the hooks, and run tests/freecam.
  The names are only for our hooks: upstream's own lines in the camera
  section keep their numbers.

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
  compare, Performance, what the engine does for speed, each ticked while
  it's on (Use P cores first, then the GPU queue, display pacing, the
  game's wait for the GPU asleep, and DXT textures as they are, each with
  its own entry), and Experimental, switches that change what the
  game does, for a comparison, shown while there is one. The start puts
  them into the environment before anything reads them. Among the logging
  switches is the CPU profile (Linux and Windows), which the start sets
  going: where each thread's time goes, by function.
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
- **Why:** nine fixes. All but the last could suit upstream; the last is
  one upstream made too, in 0.5.9:
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
  - **The guest heap has a lock of its own** (xbox_memory_layout.c). Three
    threads allocate outside the guest lock: a new worker allocates its TIB
    in `bridge_thread_main` before it takes the guest lock, and the timer
    thread and the USB controller's thread allocate theirs with no lock at
    all. Two allocations at once read the same bump pointer and could get
    the same address. JSRF hung on its loading screen that way after Restart
    on the Ally X, when one worker's stack was laid over another's, and
    later crashed at start-up after Restart, when the timer thread's TIB and
    the main thread's 1 MB allocation overlapped and every later block
    landed inside the 1 MB one. xbox_HeapAlloc, xbox_HeapFree and the
    lookups that walk the block table now take a lock of their own, set up
    in xbox_MemoryLayoutInit. tests/heap_lock (ours) tests it: four threads
    allocating at once broke the block table without the lock, and with it
    80,000 rounds hold.
  - **A vblank raised while the last is completed is kept**
    (xbox_memory_layout.c). The GPU thread completes an acknowledged vblank
    while the timer thread raises the next. It read PCRTC_INTR_0 once, then
    stored it whole and cleared PMC_INTR_0, and wiped a vblank raised in
    between: the title's DPC found nothing, and that vblank was lost. Now
    each register is cleared only if it still holds what was read, and
    PMC_INTR_0 only if a second look at PCRTC_INTR_0 finds no vblank
    pending. The log says when one was kept. Most often in frame pacing's
    last spin: a test lost 0.4 to 0.9% of the vblanks raised in it, and
    none with the fix. Upstream fixed the same in 0.5.9 (e67a46d).
    tests/vblank_ack (ours) tests it.
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
  (Developer > Performance > GPU queue, unticked) gives the old way, to
  compare.
  The generated code's recomp_types.h has to be the new one, and the
  pipeline never overwrites it: delete it and generate it again. With an
  old one the flush waits as before, and the queue's report says so.
  tests/nv2a_queue (ours) tests it, D3D's wait for the GPU included: twenty
  thousand waits against the real walk without a wake-up lost, and a wrap
  with 499 KB of tail, all of it walked. Could suit upstream.
- **When picking:** keep ours. If upstream changes how the GPU thread sets
  DMA_GET or the device window's bounds, fit ours around it, and run
  tests/nv2a_queue.

### The game's wait for the GPU, asleep

- **Files:** port/recomp_manual.c (`sub_00191440`, an override, and its
  names), src/kernel/nv2a_pb_exec.c (`nv2a_pb_wait_fence` and
  `fence_written`, the call after a fence is written, and a line in
  `nv2a_pb_exec_report`).
- **Why:** D3D waits for room in the push buffer by spinning on its fence
  word when the GPU is less than 32 KB from the fence it needs
  (`sub_00191440`). With the game ahead of the GPU thread (above), that spin
  lasted until the GPU thread came back from frame pacing's sleep: 55 to 71%
  of the game thread's time in the garage and the menus on the Ally X. The
  override makes the game's own calls up to the spin, in the game's order,
  and then sleeps with the guest lock given up until the GPU thread writes
  the fence. The game's code runs after it, finds the fence there and
  returns. No wake-up is lost: the GPU thread looks for a waiter after each
  fence it writes, and the waiter reads the fence after it arms, with a full
  barrier on each side. Where a thread can't sleep (at raised IRQL, before
  the first fence, or while another thread waits) it spins as before.
  `RECOMP_FENCE_WAIT=0` (Developer > Performance > Sleep while waiting
  for the GPU, unticked) spins always, to compare. The report every 10 s
  says how many waits slept and for how long. tests/fence_wait (ours)
  tests both halves. Could suit upstream.
- **When picking:** keep ours. If upstream changes the executor's fence
  write, keep `fence_written` right after it. If it overrides
  `sub_00191440` too, stop and ask. Run tests/fence_wait.

### The game on the P cores

- **Files:** port/main.c (a call in `host_main`, just before the game
  starts), src/nv2a_gl/nv2a_gl.c (a call at the end of `ensure_ready`),
  src/platform/win32_compat.c (`thread_unpin`, at the start of each thread
  it starts), port/CMakeLists.txt (port/pcores.c).
- **Why:** Developer > Performance > Use P cores (Linux, on by default;
  `RECOMP_P_CORES=0` turns it off). The game thread and the GPU thread each
  sleep for part of every frame, and the system may wake them on a compact
  core, where the same work takes longer: the Ally X has three P cores (Zen
  5, up to 5.0 GHz) and five compact Zen 5c (up to 3.3 GHz). Our own
  port/pcores.c tells the kinds apart by each core's highest clock, and the
  two calls keep those two threads on the P cores, the renderer's once its
  GL context is made, so that the driver's own threads run anywhere. The
  clocks, the power limit and the power profile are left as the player set
  them. A thread starts on its starter's cores, so win32_compat.c puts each
  thread the game starts (CreateThread, timers, the thread pool) back on all
  of them. The CPU profile says how much of each thread's time was on the P
  cores. In three runs on the Ally X, frames late at the flip went from 30
  to 14, and none took 30 ms or more, where each run without it had one or
  two. tests/pcores (ours) tests it. Could suit upstream.
- **When picking:** keep the two calls, `thread_unpin` and its three
  callers, and the file in port/CMakeLists.txt. If upstream starts threads
  another way, unpin them there too. Run tests/pcores, tests/developer and
  tests/cpuprof.

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

### The two sides of the sound, logged

- **Files:** src/apu/apu_dsp.c (an `apu_ch_frame` call after the fold to
  stereo), src/apu/apu_vp.c (each bin's gain kept in `gain`, and an
  `apu_ch_voice` call after the mix into bins), src/apu/CMakeLists.txt
  (apu_channels.c).
- **Why:** Developer > Logging > RECOMP_APU_CHANNELS. The sound sometimes
  drops to one side, and nothing in the log said what either side carried.
  With the switch on, the log says when one side goes quiet while the other
  plays, and which voices and mixbins were playing then, with a summary
  every 10 s. Off, it costs a test of one flag per frame and per voice. Our
  own files: src/apu/apu_channels.c and .h, and tests/apu_channels. Could
  suit upstream.
- **When picking:** keep the call in `mcpx_apu_dsp_frame` after the loop
  that writes `frame_buf`, and the one in `voice_process` after the mix into
  bins, and run tests/apu_channels.

### The sound as xemu plays it

- **Files:** src/apu/apu_vp.c (`mcpx_apu_vp_init` reads the switch; the VP
  monitor output at the end of `mcpx_apu_vp_frame` writes `frame_buf`
  instead of adding to it, and leaves the mixbins), src/apu/apu_dsp.c (the
  `apu_ch_frame` call after the fold, whichever way the sound was made).
- **Why:** Developer > Engine switches > RECOMP_APU_MIX=xemu. Our sound
  follows the title's routing into the mixbins and folds them to stereo
  with a formula of our own, since the GP and EP that do that on the console
  are stubbed. xemu, with Use DSP off as it comes set, plays each voice's
  own left and right at the loudest of its eight volumes instead, and the
  3D voices through HRTF. With the switch the sound is made that way here
  too, to tell a side lost in the routing or the fold from one lost before
  or after it. The VP monitor added each sub-frame to what was there, which
  xemu clears after each frame and we don't, so it piled up. Our own
  files: tests/apu_mix. Could suit upstream.
- **When picking:** keep ours, and run tests/apu_mix.

### Voices mixed once the title has unlocked them

- **Files:** src/apu/apu_vp.c (`mcpx_apu_vp_frame`, and a
  `[APU] voice locks` line with RECOMP_APU_STATS).
- **Why:** the title brackets a batch of changes to a voice with
  VOICE_LOCK, and makes them from its own thread while the APU's frame
  thread mixes. Every active voice was mixed as the lists were walked,
  locked or not, so a voice could be mixed half-changed, and what the frame
  wrote back to it (its play position and envelope) could overwrite what
  the title had just set. xemu waits until none of the voices it is about
  to mix is locked (`voice_work_dispatch`), and now this does too: the walk
  reports the idle voices as before and gathers the active ones, then the
  frame waits and mixes them. It gives up the frame if the APU is being
  paused. tests/apu_mix (ours) tests it. Could suit upstream.
- **When picking:** keep the walk, the wait and the mix as separate steps
  in `mcpx_apu_vp_frame`, and run tests/apu_mix.

### Frames spent loading, shown

- **Files:** src/nv2a_gl/nv2a_gl.c (the `M_FLIP_STALL` case, and
  `g_flip_drawn`).
- **Why:** the window was given a frame at a flip only if something had
  been drawn since the last pass ended. On a frame it spends loading, JSRF
  copies its off-screen frame into the display buffer, clears the
  off-screen surface, which ends a pass, and flips with no scene. So each
  load held the last picture for a second or more, where the console shows
  the cleared surface: the brief black between shots. Now a flip shows a
  frame when a display buffer was drawn since the last flip (the case's own
  `best`), and a `[GL] ... drew no scene after their pass ended` line for
  each run of them says how many there were and what was shown. A flip with
  nothing drawn at all is held and paced as before. tests/flip_load (ours)
  tests it. Could suit upstream: any title that copies into its display
  buffer on a loading frame would freeze the same way.
- **When picking:** keep the condition `S.since_present || g_flip_drawn`,
  and run tests/flip_load.

### Function starts that read the flags, refused

- **Files:** tools/disasm/engine.py (`reads_flags_on_entry`, the check
  `probes_as_callback_body` made inline for `require_entry_frame`),
  tools/disasm/functions.py (`_pass_imm_ref_targets`),
  tools/disasm/test_imm_ref_boundaries.py.
- **Why:** the function finder takes an immediate that lands in code
  nobody has claimed as the address of a function. JSRF's disk code passes
  the I/O control code 0x74004 to the kernel, which is also an address
  inside the result banner's update, a method the finder only found later.
  It split the update there, between a `cmp` and its `jne`, and the
  generated code lost the compare: the banner's last step always ended as
  if A had been pressed, so "You Win" and "Cleared" ended a second in. No
  function begins by reading the flags, since no caller hands it any, so
  such a target is now refused, as table entries already were. In JSRF
  only that function's generated code changes. Could suit upstream.
- **When picking:** keep the check in `_pass_imm_ref_targets`, and run
  `python3 -m pytest tools/disasm`.

### The sound library's stream errors, logged

- **Files:** port/recomp_manual.c (`sub_0013A6B0` and `sub_0013D250`,
  hooks on the two recoveries CRI's ADXT watchdog runs, and the names of
  the fields they read; `sub_0013B0A0` and `sub_0013B0E0`, hooks on CRI's
  lock and unlock; a call each in the hooks in `sub_00116E30` and
  `sub_0006F730`), src/kernel/kernel_bridge.c
  (`xbox_guest_lock_on_handover`, and its call in `xbox_guest_lock_acquire`
  when the lock changes hands), port/CMakeLists.txt (port/adx_err.c).
- **Why:** Developer > Logging > JSRF_ADX_ERR_LOG. A player's game stopped
  on its "disc can't be read" path on Rokkaku-dai Heights (issue #1): the
  game's check of its streams, on the main thread, read an error code that
  the watchdog sets only while it recovers a stream, and on the console
  the main thread can't run then. With the switch on, the log has a line
  for each of the watchdog's recoveries: the stream and its file, how long
  it took, and each thread that took the guest lock meanwhile, with its
  priority. When the game raises the error, it says whether a recovery was
  running. Recoveries are rare, so the same is counted over every hold of
  CRI's lock, which keeps every other thread out on the console: every
  10 s, the holds, who held them, and each thread that took the guest lock
  during one. Off, each hook tests one flag. Our own files: port/adx_err.c
  and .h, and tests/adx_err.
- **When picking:** keep the calls before and after the two recoveries' and
  the lock and unlock's generated bodies, before the sound manager's update
  and in the error's hook. If upstream changes `xbox_guest_lock_acquire`,
  keep the call where the lock changes hands (`g_slice_owner`). Run
  tests/adx_err and tests/developer.

### Suspend and resume keep the guest lock, as on the console

- **Files:** src/kernel/kernel_bridge.c (`kernel_call_blocks` no longer
  lists NtResumeThread and NtSuspendThread; `bridge_NtSuspendThread` gives
  the lock up only when a thread suspends itself; `bridge_NtResumeThread`
  hands it over only to a thread above the caller's priority, through
  `guest_lock_hand_over_to_resumed`; the time slice's hand-over is a
  function of its own, `guest_lock_hand_over`, used by both; the [LOCK]
  report counts the resumes' hand-overs, and the stall report still counts
  the two calls as waits), src/kernel/kernel_thread.c and kernel.h
  (`xbox_thread_is_current`).
- **Why:** issue #1. On the console, suspending another thread lets no
  other thread run, and resuming one switches to it only if it has a higher
  priority. CRI's lock relies on that: it raises its holder to
  time-critical, then resumes a spinner, and the unlock suspends it. Giving
  the guest lock up around both calls let the game's main thread run
  inside CRI's lock in about one hold in 90 on the Ally X and one in 25 on
  Windows, and into the ADX watchdog's recovery of a stream, where its
  check of the stream's error code can stop the game on its "disc can't be
  read" path. A thread that suspends itself still gives the lock up, and
  whether it is the calling thread is asked of the thread, since CRI's idle
  worker suspends itself through its own handle. Could suit upstream:
  nothing in it is specific to JSRF. Our own file: tests/guest_suspend.
- **When picking:** keep 224 and 231 off `kernel_call_blocks`, and the
  decisions in the two bridges. If upstream changes the time slice's
  hand-over, keep it in `guest_lock_hand_over`. Run tests/guest_suspend.

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
