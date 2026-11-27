# Graffiti saves

How the game saves a graffiti design painted in its garage, as read from its
own code in the `default.xbe` JSRF-PC supports. JSRF-PC Tools writes designs
from a player's pictures in this layout (`launcher/tools/graffiti_save.c`),
and its saves page re-signs designs brought from another emulator or a
console (`jsrf_graffiti_resign`). Only addresses, sizes and what the code
does are given here; nothing of the game's is in the repository.

## In short

- A design is a save named `GRAFFITI001` to `GRAFFITI280`, one folder each,
  holding `save.dat`, `SaveMeta.xbx` and `SaveImage.xbx`.
- `save.dat` is a 16-byte header, seven HMAC-SHA1 signatures and six blocks.
- The signatures are XAPI's roamable ones, keyed with the kernel's
  `XboxSignatureKey`, which is 16 zero bytes in JSRF-PC.
- The folder's name is XAPI's hash of the save's name, with no key in it.
- Before the game draws a design, it checks the header's signature and the
  wall texture's, then the `JSRF` tag. A design signed under another key,
  such as a console's, is left out until it is signed again.

## The signatures

XAPI is linked into `default.xbe`, so its signature code is the game's own.

| Address | XAPI function | What it does |
|---|---|---|
| 0x146C44 | `XCalculateSignatureBegin(flags)` | Calls the `Ex` form with the certificate's own title ID |
| 0x146B52 | `XCalculateSignatureBeginEx(flags, title ID)` | For the game's own title ID, the key is `XboxSignatureKey` (kernel ordinal 325); for another, its entry in `XboxAlternateSignatureKeys`. Starts HMAC-SHA1 with that 16-byte key |
| 0x146BCD | `XCalculateSignatureUpdate` | SHA-1 update |
| 0x146BE7 | `XCalculateSignatureEnd` | Finishes the HMAC. With flags bit 0 (not roamable), it then runs a second HMAC with `XboxHDKey` over the first one's result |

The graffiti writer passes flags 0 to all seven, so each is
HMAC-SHA1(`XboxSignatureKey`, data), the same on any console. JSRF-PC's
kernel gives 16 zero bytes for the key (`src/kernel/kernel_bridge.c`). A real
JSRF-PC save checks out that way: `JSRFDATA.SAV`'s first 20 bytes are
HMAC-SHA1 with a zero key over the rest of the file.

## The folder's name

XAPI names a save's folder from the save's name (0x146C6C): for each UTF-16
unit `c` of the name, in order, from 0,

    value = (value * 65536 + c) mod 0xFFFFFFFFFFC5

in 64 bits, written as 12 upper-case hex digits. `GRAFFITI001`'s folder is
always the same, whoever saved it. `jsrf_folder_name` in
`launcher/tools/jsrf_save.c` works it out.

## save.dat

Written by 0x12CCB0, to `U:` like any save.

| Bytes | What |
|---|---|
| 0 to 15 | The header |
| 16 to 155 | Seven signatures of 20 bytes: the header's, then one for each block, in order |
| 156 on | The six blocks, each the size its size class gives (the table at 0x2285B0) |

### The header (0x12EA88 to 0x12EB08)

| Byte | What |
|---|---|
| 0 | The size class: 0 SS, 1 S, 2 M, 3 L, 4 XL |
| 1 | The slot byte: the save function's first argument times 28, plus the slot, kept to a byte. The argument comes through a virtual call that wasn't traced; saves the game writes have 28 plus the slot, so it is 1 |
| 2, 3 | 0 |
| 4 to 7 | `JSRF` |
| 8 to 15 | 0 |

From slot 228 on, 28 plus the slot no longer fits a byte. The sheet builder
doesn't read byte 1, but what the garage's listing makes of it there hasn't
been read, so JSRF-PC Tools uses slots 0 to 227 only
(`JSRF_GRAFFITI_SLOTS_SAFE`).

### The blocks

| Block | What | SS, S | M | L | XL |
|---|---|---|---|---|---|
| 1 | The preview, RGB565, 32 rows | 2,048 | 3,072 | 4,096 | 8,192 |
| 2 | The design, RGB565 | 32,768 | 262,144 | 262,144 | 524,288 |
| 3 | The editor's canvas, 4 bytes a pixel | 65,536 | 524,288 | 524,288 | 1,048,576 |
| 4 | The wall's texture, DXT3 | 16,384 | 131,072 | 131,072 | 262,144 |
| 5 | The editor's stickers | 720 | 720 | 720 | 720 |
| 6 | The editor's last 16 bytes | 16 | 16 | 16 | 16 |

The designs are 128 by 128 (SS and S), 384 by 256 (M), 512 by 256 (L) and
1024 by 256 (XL). SS and S differ only in their class and how the game sizes
them on a wall.

What each block holds:

1. **The preview** (0x120896 to 0x1208C7): point-sampled from the design,
   every 4th pixel for SS and S and every 8th for the rest, from the top
   left, no averaging: 32 by 32, 48 by 32, 64 by 32 and 128 by 32.
2. **The design** (0x120860 to 0x120937, the editor's finish): RGB565, read
   from the editor's 16-bit target and packed at the design's own width. M's
   384-pixel rows leave the block's last 64 KB as padding. 0xC618 marks a
   clear pixel (0x12DFC0).
3. **The editor's canvas** (made at 0x1304C9, loaded at 0x12EB90): the paint
   canvas, a square texture 128, 512 or 1024 pixels a side, linear, B G R A
   in memory. The block holds its first 128 rows (SS, S) or 256 rows (the
   rest) at the canvas's own pitch, so M's design sits in the left 384
   pixels of 512-pixel rows. Loading copies the block straight back in.
4. **The wall's texture** (0x12DFC0): made from the RGB565 design, each
   channel widened to 8 bits by a plain shift, alpha 0 where the pixel is
   0xC618 and 255 elsewhere, then compressed to DXT3 in 128 by 128 tiles.
   Linear, 16 bytes a 4 by 4 block, packed at the design's width, so M's is
   96 blocks to a row and the block's last 32 KB is padding.
5. **The stickers** (the editor's reset at 0x122240 and 0x1223B0): 15 of 48
   bytes. Each: an int32 id at 0 (-1 for none, which isn't drawn), a byte at
   4 and words at 6 and 8 (0), floats x at 12 and y at 16 from the size's
   grid (0x225DE8, 5 across by 3 down), 10.0 at 20, three 0.0 at 24 to 32,
   and the size's scale three times at 36 to 44 (0x226040: 0.7, 0.7, 0.6,
   0.5, 0.3 by class).
6. **The last 16 bytes** (0x122367 to 0x12239D): four int32s, 0, 0, 0 and 3,
   for a fresh design.

## The other two files

- **SaveMeta.xbx:** XAPI's, naming the save (`GRAFFITI` and the slot plus 1
  in three digits, as UTF-16: 0x12CB40). `jsrf_meta_build` writes one.
- **SaveImage.xbx:** the size's icon, `D:\Media\Mark\SaveImageSS.xbx`,
  `SaveImage_S.xbx`, `SaveImage_M.xbx`, `SaveImage_L.xbx` or
  `SaveImageXL.xbx` (0x1DDA8C to 0x1DDB0C). JSRF-PC Tools copies it from the
  player's own game files when it imports, and never ships it.

## The slots

The garage's own-designs tab (0x121CDD and 0x125744) has 10 pages of 7 by 4,
so 280 slots:

    slot = (page * 4 + row) * 7 + column

It keeps a byte a slot for whether it holds a design (+0x90E) and its size
class (+0xA26).

## What the game checks

The sheet builder's reader, sub_0002D570, works out the header's signature
again and compares it with the stored one (0x2D773 to 0x2D819), then the
wall texture's, then reads the `JSRF` tag (0x2D83A to 0x2D853). If any
differs, it closes the file and leaves the design out. It doesn't read the
other blocks' signatures, or byte 1.

## Still open

- What the garage's listing does with byte 1 from slot 228 on, where 28 plus
  the slot wraps.
- Whether the listing checks the signatures as the sheet builder does.

A design saved from the game's editor past slot 228 would settle both.
