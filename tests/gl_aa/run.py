"""The OpenGL backend's anti-aliasing (SMAA, multisampling) on a headless EGL
context: test_main.c with nv2a_gfx_gl.c and nv2a_smaa.c as the engine builds
them, once for OpenGL 3.3 core and once for OpenGL ES (NV2A_GLES).

Linux, with EGL and Mesa (llvmpipe is enough): libegl-dev, libgl-dev,
libgles-dev. Skips where there is no EGL context to be had.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
sources = ["tests/gl_aa/test_main.c", "src/nv2a_gl/nv2a_gfx_gl.c", "src/nv2a_gl/nv2a_smaa.c",
           "src/nv2a_gl/nv2a_shader_cache.c", "src/nv2a_gl/nv2a_uniforms.c"]
cc = sys.argv[1] if len(sys.argv) > 1 else "cc"
failed = 0
with tempfile.TemporaryDirectory(prefix="gl-aa-") as directory:
    for name, flags, libs in (("OpenGL", [], ["-lGL"]),
                              ("OpenGL ES", ["-DNV2A_GLES=1"], ["-lGLESv2"])):
        exe = str(pathlib.Path(directory) / ("test-" + str(len(flags))))
        subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Isrc/nv2a_gl"] + flags
                       + sources + ["-o", exe, "-lEGL"] + libs + ["-lm"], cwd=root, check=True)
        print("---", name)
        r = subprocess.run([exe], timeout=300)
        failed += r.returncode != 0
sys.exit(1 if failed else 0)
