#!/usr/bin/env python3
"""
SMAA.hlsl, made into the two strings the renderer compiles: nv2a_smaa_src.h.

    python3 src/nv2a_gl/smaa/embed.py

SMAA.hlsl is the reference implementation, kept here as it was published
(see README.md). Its comments are dropped -- they are most of the file, and a
diagram in them is not ASCII, which a GLSL compiler may refuse even in a
comment -- and what is left goes in twice:

  nv2a_smaa_glsl  as it is: SMAA_GLSL_3 maps it onto GLSL 3.30 and GLSL ES 3.00.

  nv2a_smaa_msl   with the few changes the Metal Shading Language needs, which
                  SMAA_CUSTOM_SL's macros cannot make: a parameter that is
                  written (HLSL's out and inout) is a reference, an array
                  parameter a pointer, a swizzle handed to one of them a
                  temporary, and step() and lerp() with a scalar beside a
                  vector go through overloads of our own, which say so rather
                  than depend on what metal_stdlib accepts. Each change is
                  checked to apply exactly as often as expected, so a
                  different SMAA.hlsl stops this script instead of producing
                  a shader that does something else.

The search texture is sampled with point filtering on Metal, where the
sampler is in the shader, as it is by the texture's own filter on OpenGL.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "SMAA.hlsl")
OUT = os.path.join(os.path.dirname(HERE), "nv2a_smaa_src.h")


def strip_comments(text):
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                sys.exit("SMAA.hlsl: a comment that does not end")
            # Keep the line count roughly honest for error messages: one
            # newline per line the comment spanned.
            out.append("\n" * text.count("\n", i, j))
            i = j + 2
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(text[i])
            i += 1
    lines = [l.rstrip() for l in "".join(out).split("\n")]
    kept, blank = [], 0
    for l in lines:
        blank = blank + 1 if not l else 0
        if blank <= 1:
            kept.append(l)
    return "\n".join(kept).strip("\n") + "\n"


def patch(text, old, new, count, regex=False):
    if regex:
        got = len(re.findall(old, text))
        res = re.sub(old, new, text)
    else:
        got = text.count(old)
        res = text.replace(old, new)
    if got != count:
        sys.exit("SMAA.hlsl: %r found %d times, expected %d" % (old, got, count))
    return res


def for_msl(t):
    # Written parameters: references.
    t = patch(t, "out float4 offset[3]", "thread float4 (&offset)[3]", 2)
    t = patch(t, "out float2 pixcoord", "thread float2 &pixcoord", 1)
    t = patch(t, "out float4 offset)", "thread float4 &offset)", 1)
    t = patch(t, "inout float2 ", "thread float2 &", 3)
    t = patch(t, "inout float4 ", "thread float4 &", 1)
    t = patch(t, "out float2 e)", "thread float2 &e)", 2)
    # Array parameters that are only read: pointers.
    t = patch(t, r"(?<![&(])float4 offset\[3\]", "thread float4 *offset", 6, regex=True)
    # A swizzle cannot be bound to a reference: the four-wide conditional move
    # spelled out, and the corner fixes given a temporary.
    t = patch(t,
              "    SMAAMovc(cond.xy, variable.xy, value.xy);\n"
              "    SMAAMovc(cond.zw, variable.zw, value.zw);\n",
              "    if (cond.x) variable.x = value.x;\n"
              "    if (cond.y) variable.y = value.y;\n"
              "    if (cond.z) variable.z = value.z;\n"
              "    if (cond.w) variable.w = value.w;\n", 1)
    t = patch(t,
              "SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), weights.rg, coords.xyzy, d);",
              "{ float2 smaa_w = weights.rg;"
              " SMAADetectHorizontalCornerPattern(SMAATexturePass2D(edgesTex), smaa_w, coords.xyzy, d);"
              " weights.rg = smaa_w; }", 1)
    t = patch(t,
              "SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), weights.ba, coords.xyxz, d);",
              "{ float2 smaa_w = weights.ba;"
              " SMAADetectVerticalCornerPattern(SMAATexturePass2D(edgesTex), smaa_w, coords.xyxz, d);"
              " weights.ba = smaa_w; }", 1)
    # Mixed scalar and vector arguments: our own overloads (the prelude).
    t = patch(t, r"\bstep\(", "smaa_step(", 10, regex=True)
    # The search texture, point-sampled.
    t = patch(t, "SMAASampleLevelZero(searchTex, ", "SMAASampleLevelZeroPoint(searchTex, ", 1)
    return t


def c_string(name, text):
    lines = ["static const char %s[] =" % name]
    for l in text.split("\n")[:-1]:
        l = l.replace("\\", "\\\\").replace('"', '\\"')
        lines.append('    "%s\\n"' % l)
    lines[-1] += ";"
    return "\n".join(lines) + "\n"


def main():
    raw = open(SRC, "rb").read()
    try:
        text = raw.decode("latin-1").replace("\r\n", "\n")
    except UnicodeDecodeError:
        sys.exit("SMAA.hlsl: cannot be read")
    body = strip_comments(text)
    if any(ord(c) > 126 or (ord(c) < 32 and c not in "\n\t") for c in body):
        sys.exit("SMAA.hlsl: something other than ASCII outside its comments")
    msl = for_msl(body)
    lic = open(os.path.join(HERE, "LICENSE.txt"), "rb").read().decode("ascii").replace("\r\n", "\n")
    head = (
        "/*\n"
        " * GENERATED by src/nv2a_gl/smaa/embed.py from smaa/SMAA.hlsl (sha256 %s):\n"
        " * the reference SMAA shader without its comments, as GLSL (nv2a_smaa_glsl)\n"
        " * and with the changes the Metal Shading Language needs (nv2a_smaa_msl).\n"
        " * Edit SMAA.hlsl or embed.py, not this, and run embed.py again.\n"
        " *\n"
        " * SMAA's licence (smaa/LICENSE.txt):\n"
        " *\n" % hashlib.sha256(raw).hexdigest()
        + "".join(" * " + l + "\n" if l else " *\n" for l in lic.strip("\n").split("\n"))
        + " */\n\n"
    )
    with open(OUT, "w", newline="\n") as f:
        f.write(head)
        f.write("#ifndef NV2A_SMAA_SRC_H\n#define NV2A_SMAA_SRC_H\n\n")
        f.write(c_string("nv2a_smaa_glsl", body))
        f.write("\n")
        f.write(c_string("nv2a_smaa_msl", msl))
        f.write("\n#endif\n")
    print("wrote %s: GLSL %d bytes, MSL %d bytes" % (OUT, len(body), len(msl)))


if __name__ == "__main__":
    main()
