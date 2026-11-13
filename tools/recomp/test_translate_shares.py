"""tools.recomp --jobs N: the files are the same for any number of shares.

translate_batch_split deals the functions out in shares, translates share 0
itself and takes the others' results from their processes
(tools/recomp/workers.py). A function's code doesn't depend on the functions
translated before it, but the lifter's two tallies (the call targets it
named, the instructions it couldn't lift) are built across the run, and the
stub file and the report come from them: each function's part comes back
with its code and is added in the functions' order, so the tallies, and the
files, are what one process makes.
"""
import os
import textwrap

import pytest

from tools.recomp.lifter import Lifter
from tools.recomp.translator import BatchTranslator
from tools.recomp.workers import Workers

BASE = 0x00100000


class _Translator:
    """Each function's code, and its part of the tallies, made from its
    address alone; some functions call an address nobody defines, and some
    have an instruction the lifter can't lift."""

    def __init__(self):
        self.owned_function_starts = {BASE + 0x30}
        self.lifter = Lifter()
        self.lifter.referenced_calls = {}
        self.lifter.unimplemented = {}

    def translate_function(self, addr, func_info):
        lifter = self.lifter
        n = (addr - BASE) // 0x10
        for target in (BASE + 0x10 * ((n * 7) % 300), 0x00900000 + 0x10 * (n % 13)):
            lifter.referenced_calls[target] = f"sub_{target:08X}"
        if n % 5 == 2:
            for m in (("bsf", "fxtract")[n % 2], "aam"):
                lifter.unimplemented.setdefault(m, []).append(addr + n % 4)
        if n % 37 == 11:
            return None
        return f"void {func_info['name']}(void) {{ /* {n} */ }}"

    def _stub_ret_bytes(self, addr):
        return 4

    def _stub_epilogue(self, addr):
        return None


def _batch():
    batch = BatchTranslator.__new__(BatchTranslator)
    batch.translator = _Translator()
    return batch


FUNCS = [(BASE + 0x10 * i, {"name": f"sub_{BASE + 0x10 * i:08X}"})
         for i in range(300)]
MANUAL = {BASE + 0x10 * i for i in (5, 64, 65, 200)}


class _InProcess:
    """Shares 1 to n-1 translated by batches of their own, as their processes
    would; those in `fail` come back failed."""

    def __init__(self, n, fail=()):
        self.n = n
        self.fail = set(fail)

    def collect(self):
        for k in range(1, self.n):
            yield k, (None if k in self.fail else
                      _batch().translate_batch_share(FUNCS, k, self.n, MANUAL))


def _files(out, workers=None):
    batch = _batch()
    stats = batch.translate_batch_split(FUNCS, str(out), chunk_size=40,
                                        manual=MANUAL, workers=workers)
    stats.pop("files", None)
    files = {name: (out / name).read_bytes() for name in sorted(os.listdir(out))}
    lifter = batch.translator.lifter
    return (files, stats, list(lifter.referenced_calls.items()),
            list(lifter.unimplemented.items()))


@pytest.mark.parametrize("workers", [
    _InProcess(2), _InProcess(3), _InProcess(4), _InProcess(7),
    _InProcess(4, fail=[2]), _InProcess(3, fail=[1, 2]),
])
def test_any_number_of_shares_makes_the_same_files(tmp_path, workers):
    one = _files(tmp_path / "one")
    assert "recomp_stubs_unresolved.c" in one[0] and one[1]["unimplemented"]
    assert _files(tmp_path / "many", workers) == one


def test_a_tally_from_before_the_run_is_kept(tmp_path):
    batch = _batch()
    batch.translator.lifter.referenced_calls[0x00A00000] = "sub_00A00000"
    batch.translate_batch_split(FUNCS[:3], str(tmp_path), workers=_InProcess(2))
    stubs = (tmp_path / "recomp_stubs_unresolved.c").read_text()
    assert "sub_00A00000" in stubs


@pytest.mark.parametrize("n", [1, 2, 3, 5])
def test_the_shares_deal_out_every_function_once(n):
    todo = list(range(1000))
    shares = [BatchTranslator.share_of(todo, k, n) for k in range(n)]
    assert sorted(x for share in shares for x in share) == todo
    assert all(share == sorted(share) for share in shares)
    sizes = [len(share) for share in shares]
    assert max(sizes) - min(sizes) <= BatchTranslator.SHARE_CHUNK


_CHILD = textwrap.dedent("""
    import sys
    from tools.recomp.workers import write_share
    args = sys.argv[1:]
    k, n = map(int, args[args.index("--share") + 1].split("/"))
    if k == 2:
        print("share 2 gives up")
        sys.exit(3)
    write_share(args[args.index("--share-out") + 1], [(k, n, args[0])])
""")


def test_a_share_runs_on_a_process_of_its_own(tmp_path, monkeypatch, capsys):
    # Each process is the module again, with the caller's arguments and
    # --share k/n; one that fails comes back as None, with its log's end.
    (tmp_path / "fake_recomp").mkdir()
    (tmp_path / "fake_recomp" / "__main__.py").write_text(_CHILD)
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    monkeypatch.chdir(tmp_path)
    monkeypatch.setenv("PYTHONPATH", root + os.pathsep
                       + os.environ.get("PYTHONPATH", ""))
    workers = Workers(["game.xbe", "--split", "1000"], 4, module="fake_recomp")
    shares = dict(workers.collect())
    assert shares == {1: [(1, 4, "game.xbe")], 2: None, 3: [(3, 4, "game.xbe")]}
    assert "share 2 gives up" in capsys.readouterr().err
    assert not os.path.exists(workers.dir)
