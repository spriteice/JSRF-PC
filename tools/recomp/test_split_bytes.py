"""tools.recomp --split-bytes: the generated files are cut by size.

By count alone the files came out from 1 to 8 MB, and a compile on many
cores lasted as long as the biggest file took. With --split-bytes a file
also ends once its code reaches that size; the files hold the same functions
in the same order either way.
"""
import random

import pytest

from tools.recomp.translator import split_chunks


def _translations(seed, n=500):
    rng = random.Random(seed)
    # Neighbours alike, parts of the title unlike: long runs of big or small.
    sizes = [rng.choice((60, 900, 9000)) for _ in range(n // 50) for _ in range(50)]
    return [(0x10000 + 16 * i, f"sub_{i}", "x" * size)
            for i, size in enumerate(sizes)]


def _bytes(chunk):
    return sum(len(code) for _, _, code in chunk)


@pytest.mark.parametrize("seed", range(3))
def test_by_count_alone_is_the_old_cut(seed):
    t = _translations(seed)
    assert split_chunks(t, 100) == [t[i:i + 100] for i in range(0, len(t), 100)]


@pytest.mark.parametrize("seed", range(3))
@pytest.mark.parametrize("size", [5000, 40000, 200000])
def test_a_file_ends_once_it_reaches_the_size(seed, size):
    t = _translations(seed)
    chunks = split_chunks(t, 1000, size)
    assert [item for chunk in chunks for item in chunk] == t
    for chunk in chunks[:-1]:
        assert _bytes(chunk) >= size
        assert _bytes(chunk[:-1]) < size
    assert _bytes(chunks[-1]) - len(chunks[-1][-1][2]) < size


def test_the_count_still_ends_a_file():
    t = _translations(0)
    chunks = split_chunks(t, 7, 10 ** 9)
    assert [len(c) for c in chunks[:-1]] == [7] * (len(chunks) - 1)


def test_sizes_come_out_alike():
    # 400 small functions, then 400 big ones, and so on.
    t = [(0x10000 + 16 * i, f"sub_{i}", "x" * (100, 5000)[i // 400 % 2])
         for i in range(2000)]
    by_count = [_bytes(c) for c in split_chunks(t, 200)]
    by_size = [_bytes(c) for c in split_chunks(t, 10 ** 6, sum(by_count) // 10)]
    assert max(by_count) / min(by_count) > 4
    assert max(by_size[:-1]) / min(by_size[:-1]) < 1.1
