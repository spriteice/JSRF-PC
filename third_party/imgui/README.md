# Dear ImGui

https://github.com/ocornut/imgui, MIT licence (`LICENSE.txt`, and NOTICE at
the top of this repository).

The in-game menu (`src/menu`) is drawn with it, through its own SDL2 and
OpenGL 3 backends.

- Version: **v1.93.0**, commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859`
- Taken as they are, unmodified: the core (`imgui*.cpp`, `imgui*.h`,
  `imconfig.h`, `imstb_*.h`), `backends/imgui_impl_sdl2.*`,
  `backends/imgui_impl_opengl3.*` with its loader, and
  `backends/imgui_impl_null.*`, which only the menu test (`tests/menu`)
  uses. The demo, the docs, the examples and the other backends are left
  out.
- Configured from `src/menu/CMakeLists.txt` with defines, not by editing
  `imconfig.h`.

To update: replace these files with the same ones from the new tag, change
the version and commit above, then build and run
`python3 tests/menu/run.py`.
