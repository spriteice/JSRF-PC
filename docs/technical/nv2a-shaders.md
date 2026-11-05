# NV2A Shader Translation

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../building-on-linux.md) and
> [project-rules.md](../project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around.

Xbox games do not ship HLSL. They configure the NV2A's fixed-function combiner
pipeline, or upload raw vertex-shader microcode. Both have to become something
OpenGL will accept, and both are translated at runtime and cached.

This document covers the two translators, in src/nv2a_gl.

## Register combiners → GLSL fragment shaders

Xbox games do not use traditional pixel shaders. They configure the NV2A's
8-stage register combiner pipeline. Each stage performs independent RGB and
alpha math — multiply, dot product, MUX — over a register file of textures,
vertex colours and constants. A final combiner blends the result.

The game sets this up as a packed descriptor:

```c
SetPixelShader(0x00000103);   /* 3 stages, tex0 = 2D, tex1 = 2D */
```

At draw time that configuration is translated into a GLSL fragment shader:

```
Stage 0: r0.rgb = tex0 * diffuse
Stage 1: r0.rgb = r0 * tex1          (environment-map modulate)
Stage 2: r0.a   = tex0.a * diffuse.a
Final:   output = r0
```

Each program is kept in the shader cache (src/nv2a_gl/nv2a_shader_cache.h),
keyed on a hash of its generated source, so a setup is compiled once, and on the
next run built before the game asks for it. Multi-texturing is covered outright.
Generated texture coordinates (sphere, reflection and normal maps) are made in
the vertex shader (nv2a_vsh.c).

## Vertex-shader microcode → GLSL vertex shaders

When a game uses programmable vertex shaders — water displacement, skeletal
animation, custom lighting — it uploads NV2A microcode rather than any
high-level source. Each instruction is 128 bits and carries a paired MAC and
ILU operation.

The translator parses that microcode and emits GLSL:

- **13 MAC ops** — `MOV`, `MUL`, `ADD`, `MAD`, `DP3`, `DP4`, `DPH`, `DST`,
  `MIN`, `MAX`, `SLT`, `SGE`, `ARL`
- **7 ILU ops** — `MOV`, `RCP`, `RCC`, `RSQ`, `EXP`, `LOG`, `LIT`
- **192 constant registers**, 12 temporaries, 16 vertex inputs
- Relative addressing through the address register (`A0`)
- The same shader cache as the combiners'

Because both halves of an instruction issue together, the translator has to
emit them so that the MAC and ILU results are written from the *pre-instruction*
register values — reading a register the paired op just wrote is the classic
way to get this subtly wrong.

## Where the code lives

| Piece | File |
|---|---|
| Combiner → GLSL | `src/nv2a_gl/nv2a_psh.c` |
| Vertex microcode → GLSL | `src/nv2a_gl/nv2a_vsh.c` |
| Texture unswizzling | `src/d3d/d3d8_swizzle.h` |

`d3d8_swizzle.h` cites xemu as the reference that pointed to its technique, and
the two translators cite xemu for the hardware's behaviour. See
[NOTICE](../../NOTICE).
