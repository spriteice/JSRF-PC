#!/usr/bin/env python3
"""Put a build's hashes in a kit: the ones every player's build must give.

    python3 launcher/kit/set_expected.py <kit dir> <engine.stamp>

<engine.stamp> is build/engine.stamp from a first start with the game, run
with this same kit (a package made from it, by launcher/firststart/package.sh).
Its generated code's and engine's SHA-256 go in the kit's expected.txt, which
the setup checks each player's build against, and the launcher checks the
engine against at every start. Then the package is made again from the kit:
expected.txt is read by the setup, never built from, so the hashes still hold.

The stamp must come from this kit (its version) and this system (its
engine's path). Hashes the kit has already are kept only if the stamp agrees:
a build that differs stops here, as the two builds must be the same bytes.
Run with any Python 3; nothing but the two files is read.
"""
import json
import os
import sys

KEYS = ("gen_sha256", "engine_sha256")


def read_expected(path):
    """expected.txt's lines, in order, as [key, value] pairs."""
    pairs = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            k, _, v = line.rstrip("\r\n").partition("=")
            if k:
                pairs.append([k, v])
    return pairs


def is_sha256(s):
    return len(s) == 64 and all(c in "0123456789abcdef" for c in s)


def main(argv):
    if len(argv) != 3:
        sys.exit(__doc__.strip().split("\n\n")[1])
    kit, stamp_path = argv[1], argv[2]
    with open(os.path.join(kit, "kit.json"), encoding="utf-8") as f:
        info = json.load(f)
    with open(stamp_path, encoding="utf-8") as f:
        stamp = json.load(f)
    if stamp.get("kit") != info["version"]:
        sys.exit("set_expected: the stamp is from kit %r, this kit is %r"
                 % (stamp.get("kit"), info["version"]))
    if stamp.get("engine") != info["engine"]:
        sys.exit("set_expected: the stamp's engine is %r, this kit's is %r (the other system's?)"
                 % (stamp.get("engine"), info["engine"]))
    for k in KEYS:
        if not is_sha256(stamp.get(k, "")):
            sys.exit("set_expected: the stamp's %s isn't a SHA-256: %r" % (k, stamp.get(k)))

    path = os.path.join(kit, "expected.txt")
    pairs = read_expected(path)
    have = dict(pairs)
    for k in KEYS:
        if have.get(k) and have[k] != stamp[k]:
            sys.exit("set_expected: the kit's %s is %s, the stamp's %s: not the same build"
                     % (k, have[k], stamp[k]))
    for p in pairs:
        if p[0] in KEYS:
            p[1] = stamp[p[0]]
    pairs = [[k, stamp[k]] for k in KEYS if k not in have] + pairs
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for k, v in pairs:
            f.write("%s=%s\n" % (k, v))
    for k in KEYS:
        print("set_expected: %s=%s%s" % (k, stamp[k], "" if have.get(k) else " (new)"))


if __name__ == "__main__":
    main(sys.argv)
