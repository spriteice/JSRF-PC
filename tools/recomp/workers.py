"""Translating on more than one process: tools.recomp --split ... --jobs N.

The translation's loop was most of tools.recomp's time once its decoding was
fast, and it is one function after another: what a function's translation
gives depends on the data and the passes run before the loop, not on the
functions translated before it. The lifter's two tallies are the exception,
the call targets it named and the instructions it couldn't lift, so each
function's part of them comes back with its code, and they are added up in
the functions' order, as one process adds them. So the functions are dealt
out in shares
(BatchTranslator.share_of), share 0 translated by the main process and each
of the others by a process of its own, which then hands its results back.

Each such process is tools.recomp again, with the same arguments and
--share k/n: it loads the data and runs the passes itself, alongside the
main process, then translates only its share and writes what it gave to a
file. A process started fresh works the same on Linux and on Windows, where
a process can't be forked with the main one's data, and its loading costs no
time, as it happens while the main process loads too. It costs memory
instead: each process holds what one holds, about 0.7 GB for a whole game,
so the caller sets N by the memory free as well as by the cores.

A share whose process fails is translated by the main process instead, so a
failure costs time and a line in the log, never the result.
"""
import atexit
import os
import pickle
import shutil
import subprocess
import sys
import tempfile


def interpreter_flags():
    """The flags this Python was started with that a share's process needs
    too: the kit runs the pipeline with -E -s, and a share must find the
    same packages."""
    f = sys.flags
    return [flag for flag, on in (("-E", f.ignore_environment),
                                  ("-s", f.no_user_site),
                                  ("-B", f.dont_write_bytecode)) if on]


class Workers:
    """Shares 1 to n-1 of a split translation, each on a process of its own,
    started as soon as this is made."""

    def __init__(self, argv, n, module=__package__):
        self.n = n
        self.dir = tempfile.mkdtemp(prefix="recomp-shares-")
        self.procs = []
        command = [sys.executable, *interpreter_flags(), "-m", module, *argv]
        atexit.register(self.stop)
        for k in range(1, n):
            out = os.path.join(self.dir, f"share{k}.pickle")
            log_path = os.path.join(self.dir, f"share{k}.log")
            with open(log_path, "wb") as log:
                proc = subprocess.Popen(
                    command + ["--share", f"{k}/{n}", "--share-out", out],
                    stdin=subprocess.DEVNULL, stdout=log,
                    stderr=subprocess.STDOUT)
            self.procs.append((k, proc, out, log_path))

    def collect(self):
        """(k, results) for each share in turn, once its process has ended:
        what translate_share gave it, or None when the process failed."""
        for k, proc, out, log_path in self.procs:
            code = proc.wait()
            results = None
            if code == 0:
                try:
                    with open(out, "rb") as fh:
                        results = pickle.load(fh)
                except (OSError, pickle.UnpicklingError, EOFError) as e:
                    print(f"  share {k}/{self.n}: its results can't be read "
                          f"({e})", file=sys.stderr)
            if results is None:
                print(f"  share {k}/{self.n}: its process failed (exit "
                      f"{code}), so it is translated here. The end of its "
                      f"log:", file=sys.stderr)
                try:
                    with open(log_path, "r", encoding="utf-8",
                              errors="replace") as fh:
                        for line in fh.readlines()[-10:]:
                            print("    " + line.rstrip(), file=sys.stderr)
                except OSError:
                    pass
            yield k, results
        self.stop()

    def stop(self):
        """End any share's process still running, and remove their files."""
        for _, proc, _, _ in self.procs:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        shutil.rmtree(self.dir, ignore_errors=True)


def write_share(path, results):
    """A share's process hands its results back: written whole, then renamed,
    so the main process never reads half a file."""
    part = path + ".part"
    with open(part, "wb") as fh:
        pickle.dump(results, fh, protocol=pickle.HIGHEST_PROTOCOL)
    os.replace(part, path)
