# Upstream

JSRF-PC is a fork of
[JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed), by
JuMatt. It takes every upstream release, and differs from upstream only
where that suits its targets: Linux first, then Windows. This file says how
upstream is merged, how JSRF-PC may differ from it, and lists every
difference.

## Branches

- **`upstream`:** upstream's history, never edited. It moves only forward,
  to an upstream release, when a merge is asked for.
- **`main`:** JSRF-PC. Upstream reaches it only through `upstream`, by a
  merge.

## Merging upstream

- **Whole releases.** A release is a version upstream has tagged, as
  `v0.5.4`, with its "Changelog: 0.5.4" commit. When a merge is asked for,
  `main` takes the newest one whole: every commit up to it, including any
  we then leave out (below). With no release since the last merge, there
  is nothing to merge: say so.
- **Only by a merge.** Upstream commits are never cherry-picked, squashed,
  or copied by hand into `main`. Git would then not know which upstream
  version `main` is based on, and every later merge would meet the skipped
  commits again.
- **A fix needed before upstream's next release,** only when the
  maintainer asks for it: merge `upstream` up to the commit that has it,
  with everything before it, never the fix alone.

### Steps

1. **Start from the latest `origin/main` and `origin/upstream`,** each
   brought up as a fast-forward. If `main` is ahead of `origin/main`, those
   are unpushed commits: say so, and ask before building on them.

   ```sh
   git fetch origin main upstream
   git checkout main && git merge --ff-only origin/main && git status -sb
   git checkout upstream && git merge --ff-only origin/upstream
   ```

2. **Move `upstream`** to the release's "Changelog: 0.5.5" commit, as a
   fast-forward. Upstream tags the release (`v0.5.5`) a commit or two
   before it, so check that the tag is in its history. If it can't
   fast-forward, upstream has rewritten its history: stop and ask.

   ```sh
   git fetch https://github.com/JuMatt/JetSetRadioFuture-Unleashed.git main tag v0.5.5
   git log --oneline -3 FETCH_HEAD     # the "Changelog: 0.5.5" commit
   git checkout upstream && git merge --ff-only <that commit>
   ```

3. **Merge it into `main`** with a merge commit named "Merge upstream
   0.5.5": `git checkout main && git merge --no-ff upstream`.
4. **Resolve each conflict** by taking upstream's change, except where an
   entry below says what we keep in that file. Where upstream now does
   what an entry does, take upstream's version and remove the entry.
5. **Check every entry** below against what upstream changed in its files,
   correct any the merge made wrong, and update the upstream version the
   list was checked against.
6. **Leave out what doesn't suit our targets,** each in its own commit
   after the merge (below).
7. **Build and test** as docs/build-and-test-reference.md says. The test
   checklist covers what upstream changed, as well as the usual regression
   spots.
8. **The merge commit's message** lists what is new from upstream, what of
   ours upstream took, each conflict and how it was resolved, and the
   entries added, changed or removed. The full diff against `main` goes
   with it as a file.

The next release's changelog entry names the upstream version and credits
its changes (docs/versioning.md).

### Leaving an upstream change out

An upstream change that doesn't suit our targets is merged with the rest,
then left out in a commit of its own, so git records that it was seen and
refused, and later merges don't bring it back:

- **A revert** of the upstream commit, or of the part of it that doesn't
  suit (`git revert`). Its message names the upstream commit and says why.
- **A change of our own instead,** only where a revert would lose
  something we need. It goes where the code belongs, as any change does: a
  game function override only when that function's own logic is the cause
  (below).
- **Either way, it gets an entry** below.

## Changing upstream files

An upstream file is one the `upstream` branch has, wherever it is:
port/main.c and port/recomp_manual.c are upstream files, port/freecam.c is
ours. JSRF-PC changes them where that serves its targets. Each change is
checked again at every merge, so it is kept small and visible:

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
  touches and how that affects later merges. Adding a file of our own
  needs no entry.
- **Could suit upstream:** an entry says so when upstream could take the
  change too. Offering it upstream is the maintainer's call.

## Where we differ from upstream

Checked against upstream 0.5.5 (`9875cf0`). Every file this lists, the
upstream files `main` changes (M) or removes (D), is named in an entry
below:

```sh
git diff --name-status --diff-filter=MD upstream main
```

### The name JSRF-PC

- **Files:** port/CMakeLists.txt, port/main.c, port/xbe_check.c and .h,
  src/nv2a_gl/nv2a_gl.c (the window's title, the settings file's folder),
  CMakeLists.txt (a comment), and the launchers: jsrf-unleashed and
  package.sh in launcher/linux/, and jsrf_launcher.c, package.sh,
  resources.rc, engine.manifest and launcher.manifest in launcher/windows/.
- **Why:** the program, its packages, window, messages and folders are
  named JSRF-PC (jsrf-pc). The Linux launcher keeps its upstream file name
  in the repository, so upstream's changes to it still merge, and is
  packaged as jsrf-pc.
- **At a merge:** keep our names, and give anything upstream adds that
  shows a name ours.

### Version from our tags

- **Files:** port/CMakeLists.txt, launcher/windows/package.sh.
- **Why:** the version comes from our tags (b1, b1.1, 1.0.0), never
  upstream's v-tags, and a beta's Windows file version is 0.0.N.M
  (docs/versioning.md).
- **At a merge:** keep ours.

### README and the packages' README.txt

- **Files:** README.md, launcher/linux/README.txt,
  launcher/windows/README.txt.
- **Why:** they are our own: Linux and Windows only, our downloads, the
  in-game menu, the keys and portable mode.
- **At a merge:** keep ours. Read upstream's changes for anything players
  or builders need, and write it into ours (build steps go in
  docs/building-on-linux.md).

### .gitignore

- **Files:** .gitignore.
- **Why:** game files in upper or lower case, Media/, UDATA/, TDATA/,
  portable/, partition images, logs and dumps can't be committed, nor
  .venv/ or BRINGUP.md.
- **At a merge:** keep ours, and add upstream's new lines.

### In-game menu

- **Files:** CMakeLists.txt, port/CMakeLists.txt (C++, and libstdc++
  linked in statically on Linux), src/nv2a_gl/CMakeLists.txt
  (`NV2A_GL_MENU`), src/nv2a_gl/nv2a_gl.c (the menu over the window, its
  Graphics rows), src/input/xinput_device.c (the menu has the pad while it
  is open, and a pad's Back opens it), CONTRIBUTING.md, NOTICE,
  LICENSES/README.md, launcher/linux/package.sh and
  launcher/windows/package.sh (Dear ImGui's licence),
  tools/linux/install_deps.sh (a C++ compiler).
- **Why:** one menu (F1, or Back) for every setting, usable with a
  controller, in a window or under gamescope. It replaces upstream's Video
  menu bar on Windows and its anti-aliasing box on F8 (both removed from
  nv2a_gl.c), whose settings it has; F8 is the free camera's. It is built
  only for the SDL window: Android, the Mac and the iPhone keep their own
  menus. Our own files: src/menu/, third_party/imgui/.
- **At a merge:** check upstream's changes to the SDL window, keys and pad
  input against the menu, give a setting upstream adds to its Video menu or
  keys a Graphics row, and run tests/menu.

### Graphics: the anisotropic filtering setting

- **Files:** src/nv2a_gl/nv2a_gl.c (`tex_aniso` takes the level in use, set
  by `aniso_start` and `aniso_take`, and the Graphics row), nv2a_gfx_gl.c
  and nv2a_backend.h (`aniso_max`, a member added to `Nv2aGfx`).
- **Why:** upstream gives every texture with mipmaps 16x anisotropic
  filtering, or `RECOMP_GL_ANISO`'s level. Ours is also a setting,
  Graphics > Anisotropic filtering, Off to 16x (16x by default, as
  upstream), kept as aniso= and changed while the game runs: upstream's
  texture cache tells textures apart by how they are sampled, so a new
  level drops the textures it reaches, which are made again when next drawn.
  `RECOMP_GL_ANISO` still wins over the setting. MSAA, which upstream took
  from this fork in 0.5.5, and SMAA, which took the place of our FXAA, are
  upstream's, with rows in the in-game menu.
- **At a merge:** the `Nv2aGfx` tables are filled in by position, and our
  members come last: a member upstream adds goes before `aniso_max`, in the
  same order in the struct and in nv2a_gfx_gl.c's table. Metal's table
  leaves ours out (NULL). Run tests/gl_backend and tests/gl_aa.

### Shader cache

- **Files:** src/nv2a_gl/nv2a_gl.c, nv2a_gfx_gl.c, nv2a_backend.h,
  nv2a_shader_cache.c and .h, and comments in nv2a_psh.h and nv2a_vsh.h.
- **Why:** upstream's disk shader cache, with a setting (on by default, as
  upstream has it). Every program it keeps is built again at each start,
  from its sources and from the NV2A state it came from, so none is built
  while the game draws, even after a driver update or a translator change.
  Old drivers' binaries files are removed, and Developer > Purge shaders
  deletes the cache.
- **At a merge:** a change upstream makes to `Nv2aVshFixed` or
  `Nv2aPshState` changes what the cache keeps: raise `SC_LAYOUT` in
  nv2a_gl.c, and note it in CHANGELOG-JSRF-PC.md. Run tests/shader_cache
  and tests/gl_backend.

### Renderer fixes: depth bias, texture timing, MSAA depth

- **Files:** src/nv2a_gl/nv2a_gl.c, nv2a_gfx_gl.c, nv2a_backend.h (three
  members added to `Nv2aRenderState`: `poly_offset`, `poly_factor` and
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
- **At a merge:** `NV2A_U_POLYOFF` is the last uniform: keep it after
  upstream's, in the same place in nv2a_uniforms.c's table. None of this
  changes `Nv2aVshFixed` or `Nv2aPshState`, so `SC_LAYOUT` stays. Run
  tests/gl_backend (test_polyoffset.c, test_msaa.c) and tests/shader_cache.

### Draw profile by step

- **Files:** src/nv2a_gl/nv2a_gl.c (`PROF_STEP`, and a `[GLPROF]` line).
- **Why:** with `RECOMP_GL_PROFILE=1`, every 16th draw is also timed step
  by step, and the log says where a draw's time goes: setup, vertices,
  uniforms, textures, state, probes, glDraw and after. The other draws
  read no more clocks than before, so the profile's other figures stay
  comparable. Could suit upstream.
- **At a merge:** keep the seven `PROF_STEP` lines at the ends of their
  steps in `do_draw_body` when upstream moves code around them.

### Full screen remembered

- **Files:** src/nv2a_gl/nv2a_gl.c.
- **Why:** full screen is kept in the settings file, as widescreen and the
  scale are, so the game starts the way it was left. Upstream starts in a
  window unless the launcher is given `--fullscreen`. Could suit upstream.
- **At a merge:** keep ours.

### Restart

- **Files:** port/main.c (`recomp_restart_self`),
  src/nv2a_gl/nv2a_shader_cache.c.
- **Why:** a restart (a new render scale, Purge shaders) passes the new
  run only the standard handles. Upstream's passes on every open file, and
  on Linux the new run kept the last run's guest memory until the game
  closed. Could suit upstream.
- **At a merge:** keep ours, and run tests/restart.

### Free camera

- **Files:** port/recomp_manual.c (hooks in `sub_00116E30`,
  `sub_001C3E14`, `sub_000A5070` and `sub_000A6100`), port/main.c and
  port/CMakeLists.txt (starting and building it), src/nv2a_gl/nv2a_gl.c
  (`nv_window_hotkey`, keys a title binds for itself: F8, in place of
  upstream's anti-aliasing box).
- **Why:** the right stick turns the camera around the player, and the
  graffiti camera stays off while it does. Our own files: port/freecam.c
  and freecam.h.
- **At a merge:** merge upstream's changes to those four functions around
  the hooks, and run tests/freecam.

### Developer menu

- **Files:** port/main.c, port/CMakeLists.txt, src/nv2a_gl/nv2a_gl.c
  (`nv_window_file_get` and `_set`: a number kept in a file beside the
  settings file, which port/developer.c uses for developer.cfg).
- **Why:** the in-game menu's Developer menu: Log to file, and the
  engine's logging switches, which the start puts into the environment
  before anything reads them. Our own file: port/developer.c.
- **At a merge:** keep the two calls in port/main.c, and run
  tests/developer.

### Portable mode

- **Files:** launcher/linux/jsrf-unleashed,
  launcher/windows/jsrf_launcher.c, src/kernel/kernel_path.c
  (`JSRF_USER_DIR`: the game's saves, E:\UDATA and E:\TDATA, in the
  portable folder). With that third folder, the path rules name the folder
  each one goes to, `PATH_GAME_DIR`, `PATH_SAVE_DIR` or `PATH_USER_DIR`,
  where upstream writes 0 or 1.
- **Why:** a folder named `portable` beside the launcher keeps the
  settings, saves, logs and cache, so the whole game can move, as to an SD
  card (docs/portable-mode.md).
- **At a merge:** keep ours, give a path rule upstream adds or changes the
  name for its 0 or 1, and run tests/user_dir.

### The Linux launcher's help

- **Files:** launcher/linux/jsrf-unleashed.
- **Why:** `--help` prints the header comment, up to the first line that
  isn't a comment. Upstream's prints a fixed range of lines (4 to 18), so a
  header that changes length cuts the help short or prints code, as ours
  did. Could suit upstream.
- **At a merge:** keep ours.

### Kernel fixes

- **Files:** src/kernel/kernel_bridge.c, kernel.h, kernel_hal.c,
  kernel_thunks.c, kernel_rtl.c, and xbox_memory_layout.c and .h.
- **Why:** six fixes, each of which could suit upstream:
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
- **At a merge:** keep each fix. Where upstream fixes the same thing, take
  upstream's and drop ours from this entry.

### Diagnostics only in Debug builds

- **Files:** port/CMakeLists.txt.
- **Why:** `RECOMP_ABI_CHECK` and `RECOMP_ICALL_FEEDBACK` put code at every
  call site of the generated code, whether used or not. Release builds
  leave them out (the options `JSRF_ABI_CHECK` and `JSRF_ICALL_FEEDBACK`,
  on in Debug builds): 36.2 MB of code instead of 46.0 MB.
- **At a merge:** keep ours.

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
- **At a merge:** leave upstream's changes to this code out, and say so in
  the merge commit. When upstream removes it too, remove this entry.

### Unused renderers and test code, removed

- **Files:** src/d3d/d3d8_gl.c (removed), src/d3d/CMakeLists.txt and
  d3d8_internal.h; src/nv2a/nv2a_pgraph_d3d11.c and .h, nv2a_pb_replay.c,
  nv2a_pb_test.c, menu_pb_*.h, menu_pushbuffer_data.h and
  font_atlas_data.h (removed), src/nv2a/CMakeLists.txt and nv2a_core.c;
  and what named them: src/nv2a/README.md, docs/technical/nv2a-shaders.md,
  src/kernel/nv2a_pb_exec.c, src/kernel/recomp_trace.c,
  src/platform/win32_compat.c, and tools/linux/install_deps.sh (libepoxy,
  which on Linux only d3d8_gl.c needed).
- **Why:** not recorded. They were already gone from the fork's first
  commit (`a7fcd0e`), and the merge of upstream 0.5.0 (`ae7f1ad`) kept them
  out. None of them runs in JSRF, which draws through src/nv2a_gl.
- **At a merge:** an upstream change to a removed file is a modify/delete
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
- **At a merge:** take upstream's text, and remove this entry.

### Our own files

- **Files:** every file upstream doesn't have, which
  `git diff --name-only --diff-filter=A upstream main` lists: among them
  port/freecam.c and port/developer.c, src/menu/, third_party/imgui/ (Dear
  ImGui, unmodified), our tests (in tests/, and
  tools/kernel_audit/test_bridge_bodies.py), CHANGELOG-JSRF-PC.md, and our
  docs in docs/.
- **Why:** they have no counterpart upstream, so they never conflict, and
  adding one needs no entry. They use upstream's code.
- **At a merge:** build them and run their tests. If upstream adds a file
  at one of these paths, stop and ask.
