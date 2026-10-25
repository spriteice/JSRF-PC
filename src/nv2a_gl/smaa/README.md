# SMAA

The reference implementation of SMAA (Enhanced Subpixel Morphological
Antialiasing; Jorge Jimenez, Jose I. Echevarria, Tiago Sousa and Diego
Gutierrez, 2012), as published at https://github.com/iryoku/smaa (commit
71c806a838bdd7d517df19192a20f0c61b3ca29d), under its MIT licence
(LICENSE.txt):

- `SMAA.hlsl` -- the shader, written for HLSL, GLSL and any other shading
  language through a few macros
- `AreaTex.h`, `SearchTex.h` -- its two look-up textures, as C arrays

They are kept as they were published, line endings included. The renderer
does not compile `SMAA.hlsl` from here: `embed.py` turns it into
`../nv2a_smaa_src.h`, which holds it as GLSL and as Metal source, and
`../nv2a_smaa.c` wraps the three passes around it. After changing either
`SMAA.hlsl` or `embed.py`, run

    python3 src/nv2a_gl/smaa/embed.py
