"""Fill the kit (launcher/kit/make_kit.sh) from the engine's CMake build.

The engine's own objects and libraries, the headers its compile command names,
the pipeline that translates default.xbe, and kit.json: the generated code's
compile command and the engine's link command, with the build's own paths
turned into placeholders that build_engine.py fills on the player's PC:

  {kit}       this kit
  {root}      the package's folder (the launcher's)
  {gen}       the generated code's folder (a folder named recomp/gen)
  {gen_root}  the folder two above it, which recomp_manual.c's include needs
  {gen_objs}  the generated code's objects, in name order
  {out}       the engine being written

Run by make_kit.sh in the build image; nothing here runs on a player's PC.
"""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

PIPELINE = ["xbe_parser", "disasm", "func_id", "abi_analysis", "recomp"]


def die(msg):
    print("kit_json: " + msg, file=sys.stderr)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    for a in ("system", "target", "version", "repo", "build", "gen", "kit", "zig", "link"):
        ap.add_argument("--" + a, required=True)
    a = ap.parse_args()
    repo = os.path.realpath(a.repo)
    build = os.path.realpath(a.build)
    gen = os.path.realpath(a.gen)
    gen_root = os.path.dirname(os.path.dirname(gen))
    kit = a.kit
    os.makedirs(os.path.join(kit, "obj"))
    os.makedirs(os.path.join(kit, "lib"))

    # ---- the generated code's compile command: port/main.c's, less its own
    # warnings (CMake gives the engine's own files -Wall -Wextra) and files.
    cc = json.load(open(os.path.join(build, "compile_commands.json")))
    main_c = [e for e in cc if os.path.realpath(e["file"]) == os.path.join(repo, "port", "main.c")]
    if len(main_c) != 1:
        die("port/main.c is not in compile_commands.json once")
    args = shlex.split(main_c[0]["command"])[1:]
    # No debug information (-g0): the link strips it anyway, and it slows the
    # compile of the generated code down.
    compile_args, inc_dirs, skip = ["-g0"], [], 0
    for i, x in enumerate(args):
        if skip:
            skip -= 1
            continue
        if x in ("-o", "-c", "-MT", "-MF"):
            skip = 1
            continue
        if x in ("-MD", "-Wall", "-Wextra"):
            continue
        m = re.match(r"^-(I|isystem)(.*)$", x)
        if m:
            path = m.group(2)
            if not path:
                path = args[i + 1]
                skip = 1
            path = os.path.realpath(path)
            if path == gen:
                compile_args += ["-I", "{gen}"]
            elif path == gen_root:
                compile_args += ["-I", "{gen_root}"]
            elif path == repo or path.startswith(repo + os.sep):
                rel = os.path.relpath(path, repo)
                inc_dirs.append(rel)
                compile_args += ["-I", "{kit}/include/" + rel.replace(os.sep, "/")]
            else:
                # SDL's, libepoxy's, the GL headers: the generated code
                # includes none of them (only its own headers and the C
                # library's), so they stay out of the kit.
                print("kit_json: the generated code's compile leaves out -I " + path)
            continue
        compile_args.append(x)
    # The headers under those folders, as they are in the repository.
    for rel in sorted(set(inc_dirs)):
        src = os.path.join(repo, rel)
        for dp, dn, fn in os.walk(src):
            dn.sort()
            for f in sorted(fn):
                if f.endswith((".h", ".inc")):
                    d = os.path.join(kit, "include", rel, os.path.relpath(dp, src))
                    os.makedirs(d, exist_ok=True)
                    shutil.copy2(os.path.join(dp, f), os.path.join(d, f))

    # ---- the link: CMake's command, its objects and libraries copied in.
    link = a.link.strip()
    link = re.sub(r"^:\s*&&\s*", "", link)
    link = re.sub(r"\s*&&\s*:$", "", link)
    if "&&" in link:
        # Windows: CMake runs the link through cmake -E vs_link_exe or adds
        # steps; only the compiler's own command is taken.
        parts = [p.strip() for p in link.split("&&")]
        cands = [p for p in parts if os.path.basename(shlex.split(p)[0]) in ("zc++", "zcc")]
        if len(cands) != 1:
            die("can't find the link in: " + a.link)
        link = cands[0]
    largs = shlex.split(link)
    driver = os.path.basename(largs[0])
    if driver not in ("zc++", "zcc"):
        die("the link is not through the Zig wrappers: " + largs[0])
    out_args, objs_done, skip = [], False, 0
    libs_copied = []
    for i, x in enumerate(largs[1:], 1):
        if skip:
            skip -= 1
            continue
        if not objs_done and out_args and out_args[-1].startswith("{kit}/obj/") \
                and not x.endswith((".o", ".obj", ".res")):
            out_args.append("{gen_objs}")   # right after the engine's own objects
            objs_done = True
        if x == "-o":
            out_args += ["-o", "{out}"]
            skip = 1
            continue
        if x.startswith("-Wl,-rpath,") or x.startswith("-Wl,-Map=") or x.startswith("-Wl,--out-implib") \
                or x.startswith("-Wl,--major-image-version") or x.startswith("-Wl,--minor-image-version"):
            # No build-tree rpath (the launcher sets the library path, as
            # today), no map, no import library.
            continue
        path = os.path.join(build, x) if not os.path.isabs(x) else x
        if x.endswith((".o", ".obj", ".res")) and os.path.isfile(path):
            name = os.path.basename(x)
            shutil.copy2(path, os.path.join(kit, "obj", name))
            out_args.append("{kit}/obj/" + name)
            continue
        if (x.endswith(".a") or ".so" in os.path.basename(x)) and os.path.isfile(path):
            name = os.path.basename(x)
            if "SDL2" in name and a.system == "linux":
                # The package's own SDL, which the engine runs with.
                out_args.append("{root}/lib/libSDL2-2.0.so.0")
                continue
            if name == "libGL.so.1":
                out_args.append("{kit}/lib/libGL.so.1")   # the stub, below
                continue
            shutil.copy2(path, os.path.join(kit, "lib", name))
            libs_copied.append(name)
            out_args.append("{kit}/lib/" + name)
            continue
        if x.startswith("-L"):
            p = x[2:]
            if os.path.realpath(p).endswith("pthlib"):
                out_args.append("-L{kit}/lib")
                for f in os.listdir(p):
                    shutil.copy2(os.path.join(p, f), os.path.join(kit, "lib", f))
                continue
            if os.path.realpath(p).endswith(os.path.join("deps", "lib")):
                continue   # SDL2's and libepoxy's: named in full below
            die("a library folder the kit doesn't have: " + x)
        if os.path.isabs(x):
            die("a file the kit doesn't have: " + x)
        if x in ("-static-libstdc++", "-static-libgcc"):
            continue   # Zig's C driver links no C++ runtime (below)
        if x == "-mwindows":
            # A window, no console, as the release's engine is: Zig's driver
            # takes the linker's own word for it (it passes -mwindows over).
            out_args.append("-Wl,--subsystem,windows")
            continue
        out_args.append(x)
    if not objs_done:
        die("no objects in the link")
    if a.system == "windows":
        # The engine's threads are winpthreads' (src/platform): llvm-mingw's
        # driver links it by itself, Zig's has to be told.
        out_args.append("{kit}/lib/libwinpthread.dll.a")

    # ---- Linux: libGL.so.1, a stub with the GL functions the engine calls
    # by name, so the link needs no GL of the player's system.
    if a.system == "linux":
        defined, undefined = set(), set()
        files = [os.path.join(kit, "obj", f) for f in os.listdir(os.path.join(kit, "obj"))] + \
                [os.path.join(kit, "lib", f) for f in libs_copied if f.endswith(".a")]
        for f in files:
            out = subprocess.run(["nm", "-P", f], check=True, capture_output=True, text=True).stdout
            for line in out.splitlines():
                p = line.split()
                if len(p) >= 2 and not p[0].endswith(":"):
                    (undefined if p[1] == "U" else defined).add(p[0])
        gl = sorted(s for s in undefined - defined if re.match(r"^gl[A-Z]", s))
        stub = os.path.join(kit, "libGL-stub.c")
        with open(stub, "w", encoding="utf-8", newline="\n") as f:
            f.write("/* libGL.so.1 for the link only: the engine runs with the system's. */\n")
            for s in gl:
                f.write("void %s(void) {}\n" % s)
        subprocess.run([a.zig, "cc", "-target", a.target, "-shared", "-s", "-Wl,-soname,libGL.so.1",
                        "-o", os.path.join(kit, "lib", "libGL.so.1"), stub], check=True)
        os.remove(stub)
        print("kit_json: libGL.so.1 stub with %d functions" % len(gl))

    # ---- the pipeline: tools/ less its tests and outputs, and recomp_manual.c.
    pipe = os.path.join(kit, "pipeline")
    for name in PIPELINE:
        src = os.path.join(repo, "tools", name)
        shutil.copytree(src, os.path.join(pipe, "tools", name),
                        ignore=shutil.ignore_patterns("test_*", "tests", "output", "__pycache__", "*.pyc"))
    for f in os.listdir(os.path.join(repo, "tools")):
        if f.endswith(".py") and not f.startswith("test_"):
            shutil.copy2(os.path.join(repo, "tools", f), os.path.join(pipe, "tools", f))
    os.makedirs(os.path.join(pipe, "port"))
    shutil.copy2(os.path.join(repo, "port", "recomp_manual.c"), os.path.join(pipe, "port", "recomp_manual.c"))
    # The runtime's register model, where tools.recomp takes it from (its
    # ../../templates/runtime), to write it beside the generated code as in
    # the repository.
    os.makedirs(os.path.join(pipe, "templates", "runtime"))
    shutil.copy2(os.path.join(repo, "templates", "runtime", "recomp_types.h"),
                 os.path.join(pipe, "templates", "runtime", "recomp_types.h"))

    zig_ver = subprocess.run([a.zig, "version"], check=True, capture_output=True, text=True).stdout.strip()
    info = {
        "kit": 1,
        "system": a.system,
        "version": a.version,
        "zig": zig_ver,
        "target": a.target,
        "compile": compile_args,
        # The menu's C++ (Dear ImGui) needs nothing of the C++ runtime
        # (port/CMakeLists.txt), so Zig's C driver links the engine: the same
        # bytes as its C++ one, without building libc++ at the player's first
        # start. Anything that came to need it would fail the link, by name.
        "link_driver": "cc",
        # No debug information: it holds the folders the build ran in, and
        # every player's engine must be the same bytes wherever it was built.
        "link": ["-s"] + out_args,
        "engine": "bin/jsrf_recomp" if a.system == "linux" else "engine/jsrf_recomp.exe",
        "pipeline": [
            ["tools.xbe_parser", "{xbe}", "--json", "game_files/default_analysis.json"],
            ["tools.disasm", "{xbe}"],
            ["tools.func_id", "{xbe}"],
            ["tools.abi_analysis", "{xbe}"],
            ["tools.recomp", "{xbe}", "--all", "--split", "1000", "--back-edge-yield",
             "--exclude-manual", "port/recomp_manual.c", "--gen-dir", "{gen}"],
        ],
    }
    with open(os.path.join(kit, "kit.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(info, f, indent=1)
        f.write("\n")
    # The hashes every player's build must give, filled in from the
    # release's own build with the game (empty: not known, and the setup
    # takes the engine built here); and the tools this kit is built with.
    with open(os.path.join(kit, "expected.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("gen_sha256=\nengine_sha256=\ntools=python-3.13.0-20241016+capstone-5.0.9+ziglang-0.13.0\n")
    print("kit_json: compile %s" % " ".join(compile_args))
    print("kit_json: link %s" % " ".join(out_args))


if __name__ == "__main__":
    main()
