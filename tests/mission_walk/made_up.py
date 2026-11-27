"""Made-up mission files for tests/mission_walk: a random story of up to nine
chapters, laid out as the game's mission files are (the header's lists at
+0x34 and +0x174 to +0x194; launcher/tools/mission_walk.c has the layout),
with what the walk has to get right along the way: dead ends, scenes'
skips, guards ("you can't leave yet"), skaters joining and leaving, sensors
on a count with lower ones out of order, sensors that always fire, puzzles
whose ways look alike to the search, a chapter's opening that leaves to the
game flow, the garage on Load Game, exits to missions that aren't there,
and entries the walk can't read. Nothing in them is the game's.

    made_up.py <folder> <seed> [chapters] [calm] [hard] [big]
    made_up.py <folder> case <name>

writes <folder>/Media/Mission/mssnCCNN.bin. calm (0 to 1): how often the
story's own missions are left without distractions; hard (0 to 1): how
often a step is a puzzle and has lower sensors; big: a big puzzle's size
past ten flags, and 25 more missions a chapter (0: none). The same seed
gives the same files on every machine: the numbers are its own (Rng), not
Python's random, whose sequences may change between versions.
"""
import os
import struct
import sys


class Rng:
    """splitmix64, with the few draws the story needs."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFFFFFFFFFF

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        return z ^ (z >> 31)

    def random(self):
        return (self.next() >> 11) / float(1 << 53)

    def randrange(self, a, b=None):
        lo, hi = (0, a) if b is None else (a, b)
        return lo + self.next() % (hi - lo)

    def randint(self, a, b):
        return self.randrange(a, b + 1)

    def choice(self, seq):
        seq = list(seq)
        return seq[self.randrange(len(seq))]

    def shuffle(self, lst):
        for i in range(len(lst) - 1, 0, -1):
            j = self.randrange(i + 1)
            lst[i], lst[j] = lst[j], lst[i]

    def sample(self, seq, k):
        pool = list(seq)
        out = []
        for _ in range(k):
            out.append(pool.pop(self.randrange(len(pool))))
        return out


NONE = object()


def word(bank, i, v):
    return bank | (i << 3) | (v << 19)


class M:
    def __init__(self, mid):
        self.id = mid
        self.cast, self.setup, self.trig, self.act, self.ev, self.exits = [], [], [], [], [], []

    def add_exit(self, typ, n, ent):
        self.exits.append((typ, n, ent))
        return len(self.exits) - 1


def entry(pre, eff, op, params=()):
    p = list(params) + [0] * (16 - len(params))
    return ("e", list(pre), list(eff), op, p)


def layout(m):
    """The file's bytes."""
    head = bytearray(0x200)
    body = bytearray()
    base = len(head)

    def put_words(ws):
        off = base + len(body)
        for w in ws:
            body.extend(struct.pack("<I", w))
        return off

    def table(entries, size, write):
        recs = []
        for e in entries:
            recs.append(write(e))
        off = base + len(body)
        for r in recs:
            assert len(r) == size, (len(r), size)
            body.extend(r)
        return off, len(entries)

    def list_rec(e):
        if e is NONE:
            return struct.pack("<21I", 0, 0, 0, 0, 0xFFFFFFFF, *([0] * 16))
        kind, pre, eff, op, p = e
        if kind == "big":       # more than 32 conditions: not read
            return struct.pack("<21I", 0, 40, 0, 0, op, *p)
        cp = put_words(pre) if pre else 0x7FFFFFF0
        sp = put_words(eff) if eff else 0x7FFFFFF0
        return struct.pack("<21I", cp, len(pre), sp, len(eff), op, *p)

    def cast_rec(c):
        typ, cid, pre, eff = c
        cp = put_words(pre) if pre else 0x7FFFFFF0
        sp = put_words(eff) if eff else 0x7FFFFFF0
        return struct.pack("<8I", typ, cid, cp, len(pre), sp, len(eff), 0, 0)

    def exit_rec(x):
        return struct.pack("<17I", x[0], x[1], x[2], *([0] * 14))

    for o, lst in ((0x174, m.setup), (0x17C, m.trig), (0x184, m.act), (0x18C, m.ev)):
        off, n = table(lst, 0x54, list_rec)
        struct.pack_into("<II", head, o, off, n)
    off, n = table(m.exits, 0x44, exit_rec)
    struct.pack_into("<II", head, 0x194, off, n)
    off, n = table(m.cast, 0x20, cast_rec)
    struct.pack_into("<II", head, 0x34, off, n)
    return bytes(head + body)


def story(rng, chapters=9, calm=0.0, hard=0.0, big=0):
    ms = {}

    def get(mid):
        if mid not in ms:
            ms[mid] = M(mid)
        return ms[mid]

    NM, NC, NG, NR, NS = 6, 4, 4, 3, 8      # a small flag space: searches stay small
    # (Mission flags NM to NM + 9 are the puzzles' and the lower sensors')

    def rflag():
        bank = rng.choice((0, 0, 1, 2, 3))
        return bank, rng.randrange((NM, NC, NG, NR)[bank])

    def rcond():
        b, i = rflag()
        return word(b, i, rng.randint(0, 1))

    def reff():
        b, i = rflag()
        if b == 2 and i == NG - 1 and rng.random() < 0.8:
            i = 0
        return word(b, i, rng.randint(0, 1))

    def distract(m, ids):
        """Entries that aren't the story's way."""
        for _ in range(rng.randint(0, 3)):
            pre = [rcond() for _ in range(rng.randint(0, 2))]
            eff = [reff() for _ in range(rng.randint(0, 2))]
            lst = rng.choice((m.setup, m.trig, m.act, m.ev))
            op = rng.choice((1, 5, 32, 214, 215, 29, 31, 228, 249, 230, 7))
            p = [rng.randrange(12), rng.randrange(3), 0, rng.randrange(3)]
            if op in (228, 230):        # a way out: only behind a flag that is seldom set
                pre = [word(2, NG - 1, 1)] + pre
            if op in (29, 31):
                p[0] = rng.choice((5, 10, 20, 25, 40))
            lst.append(entry(pre, eff, op, p))
        if rng.random() < 0.3:      # a guard: you can't leave yet
            k = rng.randrange(NM)
            other = [rcond() for _ in range(rng.randint(1, 2))]
            m.act.append(entry([word(0, k, 1)] + other, [word(0, k, 0)], 1))
        if rng.random() < 0.2:
            m.ev.append(NONE if rng.random() < 0.5 else ("big", [], [], 1, [0] * 16))
        if rng.random() < 0.2:      # an unreadable condition word
            m.act.append(entry([0x7 | (3 << 3)], [rcond()], 1))
        if rng.random() < 0.3:      # a skater's cast record
            s = rng.randrange(NS)
            m.cast.append((2, 0x80000000 | s, [rcond()] if rng.random() < 0.5 else [], [rcond()]))
        if rng.random() < 0.2:
            m.cast.append((1, 5, [], [rcond()]))
        if rng.random() < 0.2:      # an exit somewhere odd
            k = m.add_exit(rng.choice((0xFB, 0xFE, 0x101, 0xFC)), rng.choice(ids) % 100 + 100 * rng.randrange(3),
                           rng.choice((0, 3, 0xFFFFFFFF)))
            m.ev.append(entry([word(2, NG - 1, 1), rcond()], [], rng.choice((230, 238)), [k]))

    for c in range(1, chapters + 1):
        opening = 101 if c == 1 else c * 100 + 96
        nn = rng.sample([x for x in range(1, 90) if x != 1 or c != 1], rng.randint(1, 4))
        way = [opening] + sorted(c * 100 + x for x in nn)
        if c > 1 and rng.random() < 0.25:
            # the chapter's 96 leaves to the game flow: on from its 97
            o = get(opening)
            k = o.add_exit(0xFE, 0, 0)
            o.ev.append(entry([], [], 230, [k]))
            if rng.random() < 0.5:
                o.setup.append(entry([], [word(2, rng.randrange(NG), 1)], 1))
            way[0] = c * 100 + 97
        if rng.random() < 0.2:
            get(c * 100)        # the chapter's 00, where Load Game's garage goes
        for a, b in zip(way, way[1:] + [None]):
            m = get(a)
            flag = rng.randrange(NM)
            sop = rng.choice((1, 5, 29, 31, 7))
            p = [rng.choice((5, 25, 40)) if sop in (29, 31) else 0]
            spre = [word(1, rng.randrange(3), 0)] if rng.random() < 0.2 else []
            m.trig.append(entry(spre, [word(0, flag, 1)] + ([word(rng.choice((2, 3)), rng.randrange(3), rng.randint(0, 1))]
                                                            if rng.random() < 0.3 else []), sop, p))
            if sop in (29, 31) and rng.random() < 0.5:       # a lower one on the same count
                m.trig.append(entry([], [word(rng.choice((1, 2)), rng.randrange(NC), 1)], sop, [p[0] // 2 + 1]))
            if rng.random() < 0.3:
                m.act.append(entry([], [word(2, rng.randrange(NG - 1), 1)], 214, [rng.randrange(NS)]))
            if rng.random() < 0.2:
                m.setup.append(entry([], [], 215, [rng.randrange(NS)]))
            if rng.random() < hard:
                # A puzzle: sensors that set and clear the mission's flags, and
                # the way on only with some of them set and one clear, so the
                # search goes through many states that look as good.
                bits = rng.sample(range(NM, NM + 6), 4)
                for x in bits[:3]:          # each of the three can be set...
                    m.trig.append(entry([], [word(0, x, 1)] + ([word(0, bits[3], 1)] if rng.random() < 0.5 else []),
                                        rng.choice((1, 5))))
                m.trig.append(entry([], [word(0, bits[3], 0)], 1))   # ...and the fourth cleared
                for _ in range(rng.randint(1, 4)):
                    on = rng.sample(range(NM, NM + 6), rng.randint(1, 2))
                    off = rng.sample(range(NM, NM + 6), rng.randint(0, 1))
                    m.trig.append(entry([], [word(0, x, 1) for x in on] + [word(0, x, 0) for x in off],
                                        rng.choice((1, 5))))
                rng.shuffle(m.trig)
                gate = [word(0, x, 1) for x in bits[:3]] + [word(0, bits[3], 0)]
            else:
                gate = []
            if rng.random() < 0.3:
                # A sensor that always fires once its flag is set
                m.trig.append(entry([word(0, flag, 1)], [word(rng.choice((2, 3)), rng.randrange(3), 1)], 32))
            if big and rng.random() < 0.5:
                # A big puzzle: ten flags, each set by a sensor of its own, all
                # wanted: a thousand and more states that look alike
                many = list(range(NM + 10, NM + 10 + 10 + big))
                for x in many:
                    m.trig.append(entry([], [word(0, x, 1)], 1))
                gate = gate + [word(0, x, 1) for x in many]
            if rng.random() < hard:
                # Lower sensors on a count, out of order, one of them opening a way out
                cnt = rng.choice((29, 31))
                low = rng.sample(range(2, 39), 3)
                for i, x in enumerate(low):
                    m.trig.append(entry([], [word(0, NM + 7 + i, 1)], cnt, [x]))
                m.trig.append(entry([], [word(0, flag, 1)], cnt, [40 + rng.randrange(5)]))
                side = m.add_exit(rng.choice((0xFB, 0xFD)), rng.choice(way) % 100, rng.choice((7, 0xFFFFFFFF)))
                m.ev.append(entry([word(0, NM + 7 + rng.randrange(3), 1), word(1, 3, 0)], [], 230, [side]))
            if b is None:
                k = m.add_exit(0xFF, 0, 0)
            else:
                ent = rng.choice((0, 1, 2, 0xFFFFFFFF))
                k = m.add_exit(rng.choice((0xFB, 0xFC, 0xFD)), b % 100 + 100 * rng.randrange(4), ent)
            op, idx = rng.choice(((230, 0), (229, 3), (238, 0), (246, 1), (248, 1)))
            p = [0] * 4
            p[idx] = k
            pre = [word(0, flag, 1)] + gate + ([word(1, rng.randrange(3), 0)] if rng.random() < 0.15 else [])
            m.ev.append(entry(pre, [word(rng.choice((2, 3)), rng.randrange(3), 1)] if rng.random() < 0.3 else [], op, p))
            if rng.random() >= calm:
                distract(m, way)
        for _ in range(rng.randint(0, 2) + 25 * (big > 0)):          # missions no exit leads to
            x = rng.choice([y for y in range(1, 90)])
            if c * 100 + x not in ms:
                m = get(c * 100 + x)
                distract(m, way)
    return ms


def case(name):
    """A story of chapter 1 alone, made by hand to show one rule of the walk;
    the walk stops at chapter 1's clear, and what it reached on the way is
    what's checked. The player's sensors are 0101's: one sets M1, another
    M5, and an event on M5 leads to 0103, so each story has a way out."""
    w = word
    m, m2, m3 = M(101), M(102), M(103)
    k2 = m.add_exit(0xFB, 2, 0)
    k3 = m.add_exit(0xFC, 3, 5)
    m.trig.append(entry([], [w(0, 1, 1)], 1))
    m.trig.append(entry([], [w(0, 5, 1)], 5))
    if name == "fired":
        # An entry the walk can't read ends what an entry with no conditions
        # follows: G2 is not set.
        m.act.append(entry([w(0, 1, 1)], [w(1, 1, 1)], 1))
        m.act.append(NONE)
        m.act.append(entry([], [w(2, 2, 1)], 1))
        m.ev.append(entry([w(0, 1, 1)], [], 230, [k2]))
    elif name == "event_waits":
        # An event that changes flags lets the actions run again before the
        # next event: the action takes M2 back first, so 0102 isn't reached.
        m.act.append(entry([w(0, 2, 1)], [w(0, 2, 0), w(2, 1, 1)], 1))
        m.ev.append(entry([w(0, 1, 1), w(0, 4, 0)], [w(0, 2, 1), w(0, 4, 1)], 1))
        m.ev.append(entry([w(0, 2, 1)], [], 230, [k2]))
    elif name == "dead":
        # A game over ends the way: 0102 isn't reached.
        m.ev.append(entry([w(0, 1, 1)], [], 228))
        m.ev.append(entry([w(0, 1, 1)], [], 230, [k2]))
    elif name == "skip":
        # The skip of a scene seen before is left out: G1 is not set.
        m.ev.append(entry([], [w(2, 1, 1)], 249))
        m.ev.append(entry([w(0, 1, 1)], [], 230, [k2]))
    elif name == "guard":
        # A "you can't leave yet" action (M8 taken back unless M2) is no guard
        # of itself: its own way, M7 then M8, leads to 0102 by M3.
        m.trig.append(entry([], [w(0, 7, 1)], 7))
        m.trig.append(entry([w(0, 7, 1)], [w(0, 8, 1)], 8))
        m.act.append(entry([w(0, 8, 1), w(0, 2, 0)], [w(0, 8, 0), w(0, 3, 1)], 1))
        m.ev.append(entry([w(0, 3, 1)], [], 230, [k2]))
    elif name == "loaded":
        # The garage on Load Game goes to the chapter's 00.
        kl = m.add_exit(0x101, 0, 9)
        m.ev.append(entry([w(0, 1, 1)], [], 238, [kl]))
        m0 = M(100)
        k = m0.add_exit(0xFD, 2, 0xFFFFFFFF)
        m0.ev.append(entry([], [w(3, 2, 1)], 230, [k]))
        yield m0
    else:
        raise SystemExit("made_up.py: no case %s" % name)
    m.ev.append(entry([w(0, 5, 1)], [], 229, [0, 0, 0, k3]))
    yield m
    yield m2
    yield m3


def main():
    if len(sys.argv) > 3 and sys.argv[2] == "case":
        out = os.path.join(sys.argv[1], "Media", "Mission")
        os.makedirs(out, exist_ok=True)
        for m in case(sys.argv[3]):
            with open(os.path.join(out, "mssn%04d.bin" % m.id), "wb") as f:
                f.write(layout(m))
        return
    out, seed = sys.argv[1], int(sys.argv[2])
    chapters = int(sys.argv[3]) if len(sys.argv) > 3 else 9
    calm = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    hard = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
    big = int(sys.argv[6]) if len(sys.argv) > 6 else 0
    rng = Rng(seed)
    out = os.path.join(out, "Media", "Mission")
    os.makedirs(out, exist_ok=True)
    for mid, m in story(rng, chapters, calm, hard, big).items():
        name = "mssn%04d.bin" % mid
        with open(os.path.join(out, name), "wb") as f:
            f.write(layout(m))


if __name__ == "__main__":
    main()
