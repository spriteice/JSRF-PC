"""tools/prompt_census.py on files made up here, in the game's container
format (MULT and NORM, then a texture item's 32-byte header and its DXT3
texels), with made-up textures: nothing of the game's is in them."""
import struct
import zlib

import pytest

from tools import prompt_census as pc

GREEN, RED, BLUE, YELLOW, GREY = 0x07E0, 0xF800, 0x001F, 0xFFE0, 0x8410


def block(c565, alpha=15):
    """A 4 x 4 DXT3 block of one colour."""
    a = (alpha | alpha << 4) & 0xFF
    return bytes([a] * 8) + struct.pack("<HHI", c565, c565, 0)


def dxt3(width, height, paint):
    """paint(bx, by) -> (colour, alpha) for each 4 x 4 block."""
    out = b""
    for by in range(height // 4):
        for bx in range(width // 4):
            out += block(*paint(bx, by))
    return out


def tex_item(width, texels, fmt=6, levels=1, ident=0x31D52524):
    head = struct.pack("<I", ident) + bytes(16) + struct.pack("<I", width) + bytes([fmt, levels]) + bytes(6)
    assert len(head) == pc.HEADER
    return head + texels


def container(magic, parts):
    """magic, count and 8 bytes, then for each item its 16-byte entry and
    itself; the entry gives where the item starts and the next entry is,
    from the container's start, and its size."""
    out = magic + struct.pack("<I", len(parts)) + bytes(8)
    for part in parts:
        entry = len(out)
        start = entry + 16
        out += struct.pack("<4I", start, start + len(part), len(part), 0) + part
    return out


def four_tiles(bx, by):
    """A 32 x 32 texture: four 8 x 8 tiles across its top, green, red, blue
    and yellow; grey under them; clear elsewhere."""
    if by < 2:
        return ([GREEN, RED, BLUE, YELLOW][bx // 2], 15)
    if by < 4:
        return (GREY, 15)
    return (GREY, 0)


@pytest.fixture
def media(tmp_path):
    root = tmp_path / "Media"
    (root / "Disp").mkdir(parents=True)
    sheet = tex_item(32, dxt3(32, 32, four_tiles))
    plain = tex_item(16, dxt3(16, 16, lambda bx, by: (GREY, 15)), ident=0x3300DB46)
    model = b"MDLB" + bytes(28)
    norm = container(b"NORM", [plain, model, sheet])
    (root / "Disp" / "Sheet.dat").write_bytes(container(b"MULT", [norm]))
    other = tex_item(64, bytes(64), fmt=5)         # a format not decoded
    (root / "Other.dat").write_bytes(container(b"NORM", [other]))
    return root


def test_items_and_textures(media):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    found = pc.textures(buf)
    assert [(w, h, f, lv) for _, w, h, f, lv in found] == [(16, 16, 6, 1), (32, 32, 6, 1)]
    # The model between them is an item, not a texture.
    assert len(pc.items(buf)) == 3


def test_key_is_the_first_levels_crc(media):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at, w, h, _, _ = pc.textures(buf)[1]
    assert pc.key(buf, at, w, h) == zlib.crc32(dxt3(32, 32, four_tiles))
    # 32 x 32 is 1024 bytes in DXT3: the head is all of it; a 16 x 16 has
    # fewer, and its head is those.
    assert pc.head(buf, at, w, h) == pc.key(buf, at, w, h)
    at, w, h, _, _ = pc.textures(buf)[0]
    assert pc.head(buf, at, w, h) == zlib.crc32(dxt3(16, 16, lambda bx, by: (GREY, 15)))


def test_decode_and_tiles(media):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at, w, h, _, _ = pc.textures(buf)[1]
    px = pc.dxt3(buf, at, w, h)
    assert px[0][0] == (0, 255, 0, 255)
    assert px[20][20][3] == 0
    assert sorted(pc.tiles(px)) == [(0, 0, 8, 8, "G"), (8, 0, 8, 8, "R"),
                                    (16, 0, 8, 8, "B"), (24, 0, 8, 8, "Y")]


def test_show(media):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at, w, h, _, _ = pc.textures(buf)[1]
    text = pc.show(pc.dxt3(buf, at, w, h), 0, 0, 32, 20, step=4).split("\n")
    assert text[1] == "   0 GGRRBBYY"
    assert text[3] == "   8 ++++++++"
    assert text[5] == "  16         "


def test_main_list_and_find(media, capsys):
    pc.main(["list", str(media)])
    out = capsys.readouterr().out.split("\n")
    grey = zlib.crc32(dxt3(16, 16, lambda bx, by: (GREY, 15)))
    assert out[0] == "Disp/Sheet.dat 0x40 16x16 DXT3 levels 1 head %08X key %08X" % (grey, grey)
    assert out[1].startswith("Disp/Sheet.dat ") and " 32x32 DXT3 " in out[1]
    assert out[2] == "Other.dat 0x20 64 format byte 5, not decoded"
    pc.main(["find", str(media), "Disp/Sheet.dat"])
    out = capsys.readouterr().out.split("\n")
    assert " 32x32 DXT3 " in out[0]
    assert sorted(out[1:5]) == ["    B 16,0 8x8", "    G 0,0 8x8", "    R 8,0 8x8", "    Y 24,0 8x8"]


def test_main_show_refuses_outside(media):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at = pc.textures(buf)[1][0]
    with pytest.raises(SystemExit):
        pc.main(["show", str(media), "Disp/Sheet.dat", hex(at), "24", "0", "16", "8"])
    # Only at a texture item's start.
    with pytest.raises(SystemExit):
        pc.main(["show", str(media), "Disp/Sheet.dat", hex(at + 4), "0", "0", "4", "4"])


def test_main_show_a_texture_with_items_after_it(media, capsys):
    # The plain 16 x 16 texture has the model and the sheet after it: its
    # size is its item's, not the rest of the file.
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at = pc.textures(buf)[0][0]
    pc.main(["show", str(media), "Disp/Sheet.dat", hex(at), "0", "12", "16", "4", "--step", "4"])
    assert capsys.readouterr().out.split("\n")[1] == "  12 ++++"
    with pytest.raises(SystemExit):
        pc.main(["show", str(media), "Disp/Sheet.dat", hex(at), "0", "16", "16", "4"])


def test_sprites(media, capsys):
    buf = (media / "Disp" / "Sheet.dat").read_bytes()
    at, w, h, _, _ = pc.textures(buf)[1]
    px = pc.dxt3(buf, at, w, h)
    # The four tiles and the grey under them are one area of 32 x 16.
    assert pc.sprites(px, 0, 0, w, h) == [(0, 0, 32, 16, 512)]
    # In a rectangle, only what is in it; fewer texels than least, nothing.
    assert pc.sprites(px, 8, 0, 8, 8) == [(8, 0, 8, 8, 64)]
    assert pc.sprites(px, 8, 0, 8, 8, least=65) == []
    pc.main(["sprites", str(media), "Disp/Sheet.dat", hex(at), "0", "8", "4", "8"])
    assert capsys.readouterr().out == "    0,8 4x8 32 texels\n"
    with pytest.raises(SystemExit):
        pc.main(["sprites", str(media), "Disp/Sheet.dat", hex(at), "0", "8"])
