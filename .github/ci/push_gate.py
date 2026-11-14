#!/usr/bin/env python3
"""The checks a commit passes before it goes onto main, in one place.

    python3 .github/ci/push_gate.py --words <file>
    python3 .github/ci/push_gate.py --words <file> --staged --message <file>

It prints nothing and exits 0 when all is well. Otherwise it prints a line
for each thing it found, and exits 1. docs/project-rules.md says when it is
run: "Before every commit", 1, and "Signed commits".

Before a push (the first form), for main against origin/main:

  - origin/main here is GitHub's main, asked for now (git ls-remote);
  - main is ahead of it in a straight line, with no merge;
  - every commit is signed (it has a gpgsig header; GitHub's ruleset on main
    checks the signature itself);
  - and the checks below, on each commit's message, its author and what it
    adds.

Before a commit (--staged): the checks below, on the message in the
--message file and on what is staged.

The checks, which are the project instructions' "Never in the repo":

  - no Claude session link, and no Claude-Session line;
  - none of the words in the --words file, one word or phrase a line (a line
    that starts with # is a comment). They are words that can't be written
    in the repository, such as the maintainer's first name, so the file is
    kept outside it: in the project's shared folder,
    release-prep/never-in-the-repo.txt. Without the file nothing is checked,
    and the script says so and fails;
  - no file .gitignore keeps out (the game's files, the generated code,
    logs), which only a forced add can stage;
  - README.md and the packages' README.txt as they were: they change only in
    a release's own commits (docs/project-rules.md, "Docs change with the
    code"), which are checked with --release.

Only what a commit adds is checked: a line it takes out, or a file it
deletes, is never a reason to refuse it.
"""

import argparse
import pathlib
import re
import subprocess
import sys

# A link to a Claude session, the id such a link ends with, and the line a
# session's own setup asks for at the end of a commit message. The id's form
# (letters and digits, with a digit and a capital among them) keeps a C name
# such as session_table from matching.
SESSION = [
    (re.compile(r"claude\.ai/(?:code|chat|share|project)\b\S*", re.I), "a Claude link"),
    (re.compile(r"\bsession_(?=[0-9A-Za-z]*[0-9])(?=[0-9A-Za-z]*[A-Z])[0-9A-Za-z]{20,}\b"),
     "a session's id"),
    (re.compile(r"^\s*Claude-Session\s*:", re.I | re.M), "a Claude-Session line"),
]
READMES = ("README.md", "launcher/linux/README.txt", "launcher/windows/README.txt")


class Stop(Exception):
    """Something the checks can't go on without."""


def git(*args, cwd=None, ok=(0,), stdin=None):
    done = subprocess.run(("git",) + args, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE,
                          input=None if stdin is None else stdin.encode("utf-8"))
    if done.returncode not in ok:
        raise Stop("git %s: %s" % (" ".join(args),
                                   done.stderr.decode("utf-8", "replace").strip()))
    return done.stdout.decode("utf-8", "replace")


def read_words(path):
    """The words file as patterns, each matching its word or phrase whole,
    in any case."""
    try:
        lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    except OSError as e:
        raise Stop("the words file can't be read (%s): nothing was checked" % e)
    words = [ln.strip() for ln in lines if ln.strip() and not ln.lstrip().startswith("#")]
    if not words:
        raise Stop("the words file %s has no word in it: nothing was checked" % path)
    return [re.compile(r"(?<![0-9A-Za-z])" + re.escape(w) + r"(?![0-9A-Za-z])", re.I)
            for w in words]


def in_text(text, words):
    """What the text has that the repository mustn't: a list of findings.
    A word found isn't printed, only counted, so the finding can be shown
    anywhere."""
    found = [what for pattern, what in SESSION if pattern.search(text)]
    hits = sum(1 for w in words if w.search(text))
    if hits:
        found.append("%d of the words file's words" % hits)
    return found


def added(diff):
    """A unified diff's added lines, as (path, line) pairs."""
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("+") and path is not None:
            yield path, line[1:]


def check_change(label, message, author, diff, names, words, release, cwd):
    """One commit, or what is staged: its message, its author, the lines it
    adds and the paths it adds or changes."""
    out = []
    for what in in_text(message, words):
        out.append("%s: its message has %s" % (label, what))
    for what in in_text(author, words):
        out.append("%s: its author or committer has %s" % (label, what))
    seen = set()
    for path, line in added(diff):
        for what in in_text(line, words):
            if (path, what) not in seen:
                seen.add((path, what))
                out.append("%s: %s adds %s" % (label, path, what))
    paths = [p for status, p in names if not status.startswith("D")]
    for path in paths:
        for what in in_text(path, words):
            out.append("%s: the path %s has %s" % (label, path, what))
    if paths:
        ignored = git("check-ignore", "--no-index", "--stdin", "-z", cwd=cwd, ok=(0, 1),
                      stdin="\0".join(paths) + "\0")
        for path in filter(None, ignored.split("\0")):
            out.append("%s: %s is a file .gitignore keeps out" % (label, path))
    if not release:
        for path in paths:
            if path in READMES:
                out.append("%s: %s changes, and this isn't a release's own commit (--release)"
                           % (label, path))
    return out


def name_status(text):
    """`--name-status` output as (status, path) pairs; a rename or a copy
    gives its new path."""
    pairs = []
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) >= 2:
            pairs.append((parts[0], parts[-1]))
    return pairs


def check_staged(message_file, words, release, cwd):
    try:
        message = pathlib.Path(message_file).read_text(encoding="utf-8")
    except OSError as e:
        raise Stop("the message file can't be read (%s)" % e)
    names = name_status(git("diff", "--cached", "--name-status", "-M", cwd=cwd))
    if not names:
        raise Stop("nothing is staged")
    diff = git("diff", "--cached", "--no-color", "--no-ext-diff", "-M", cwd=cwd)
    return check_change("staged", message, "", diff, names, words, release, cwd)


def check_push(remote, words, release, cwd):
    out = []
    ours = "refs/remotes/%s/main" % remote
    here = git("rev-parse", "--verify", "--quiet", ours, cwd=cwd, ok=(0, 1)).strip()
    if not here:
        raise Stop("no %s/main here: fetch it first" % remote)
    listed = git("ls-remote", remote, "refs/heads/main", cwd=cwd).split()
    there = listed[0] if listed else ""
    if there != here:
        out.append("%s/main here is %s, and on the remote it is %s: fetch, and check again"
                   % (remote, here[:7], there[:7] or "missing"))
    main = git("rev-parse", "--verify", "--quiet", "refs/heads/main", cwd=cwd, ok=(0, 1)).strip()
    if not main:
        raise Stop("no main here")
    if subprocess.run(("git", "merge-base", "--is-ancestor", here, main), cwd=cwd).returncode:
        out.append("main isn't ahead of %s/main in a straight line" % remote)
        return out
    commits = git("rev-list", "--reverse", "%s..%s" % (here, main), cwd=cwd).split()
    if not commits:
        raise Stop("main is %s/main: there is nothing to push" % remote)
    for commit in commits:
        label = commit[:7]
        raw = git("cat-file", "-p", commit, cwd=cwd)
        header, _, message = raw.partition("\n\n")
        lines = header.splitlines()
        if sum(1 for ln in lines if ln.startswith("parent ")) != 1:
            out.append("%s: a merge, or a commit with no parent" % label)
            continue
        if not any(ln.startswith("gpgsig ") or ln.startswith("gpgsig-sha256 ") for ln in lines):
            out.append("%s: unsigned" % label)
        author = "\n".join(ln for ln in lines if ln.startswith(("author ", "committer ")))
        names = name_status(git("diff-tree", "--no-commit-id", "--name-status", "-r", "-M",
                                commit, cwd=cwd))
        diff = git("diff-tree", "--no-commit-id", "-p", "--no-color", "--no-ext-diff", "-r",
                   "-M", commit, cwd=cwd)
        out += check_change(label, message, author, diff, names, words, release, cwd)
    return out


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--words", required=True,
                        help="the file of words the repository mustn't hold")
    parser.add_argument("--staged", action="store_true",
                        help="check what is staged, before a commit")
    parser.add_argument("--message", help="with --staged: the proposed commit message's file")
    parser.add_argument("--release", action="store_true",
                        help="a release's own commits, which may change the READMEs")
    parser.add_argument("--remote", default="origin")
    args = parser.parse_args(argv)
    try:
        if args.staged and not args.message:
            raise Stop("--staged needs --message <file>")
        words = read_words(args.words)
        top = git("rev-parse", "--show-toplevel").strip()
        if args.staged:
            found = check_staged(args.message, words, args.release, top)
        else:
            found = check_push(args.remote, words, args.release, top)
    except Stop as e:
        print("push gate: %s" % e)
        return 1
    for line in found:
        print(line)
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
