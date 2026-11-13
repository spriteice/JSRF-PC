# xbox_apu — MCPX APU Audio Emulation

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the APU works. Where it differs from
> how JSRF-PC is built or changed,
> [building-on-linux.md](../../docs/building-on-linux.md) and
> [project-rules.md](../../docs/project-rules.md) are right, and this doc is
> out of date there: say so. The generated code is never edited, and a bug
> is fixed where it starts, not stubbed out or guarded around.

Hardware emulation of the Xbox's MCPX APU (Audio Processing Unit), extracted from [xemu](https://github.com/xemu-project/xemu). The APU is a custom chip with three processors:

- **VP (Voice Processor)** — 256 hardware voices, ADPCM/PCM decode, pitch shifting, envelopes, HRTF 3D audio. xemu's, in `apu_vp.c`.
- **GP (Global Processor)** — the DSP that runs the title's effects image (reverb, the 3D sounds' crosstalk, the sends and mixers). Its DSP isn't run (`apu_dsp.c`). In its place `apu_gpfx.c` follows the effects settings the title gives it, and JSRF-PC's own reverb and 3D placement run from them (`apu_reverb.h`, `apu_hrtf.h`; Audio > Audio effects).
- **EP (Encode Processor)** — the DSP for AC3/DTS encoding. Not run: the front mixbins go straight to the output (`apu_dsp.c`).

The sound goes out through SDL2's audio (`apu_sdl.c`), on Linux and Windows, at 48 kHz stereo 16-bit. If SDL's device doesn't open, `apu_core.c` falls back to `waveOut`, on Windows only.

## How the engine reaches it

- **Start:** port/main.c calls `mcpx_apu_init_standalone` with the guest's
  physical memory, the window at guest VA 0x80000000, as the APU addresses
  physical memory.
- **Registers:** the title's loads and stores to the APU's registers
  (0xFE800000 on) reach `mcpx_apu_mmio_read` and `mcpx_apu_mmio_write`
  through src/kernel's `kernel_mmio.c`, which port/main.c points at its APU
  handlers.
- **Frames:** the VP runs in its own thread (`apu_core.c`), at the
  hardware's frame rate, paced on the output's ring (`apu_sdl.c`).

## Files

| File | Purpose |
|------|---------|
| `apu.h` | **Public API** — init, shutdown, MMIO |
| `apu_state.h` | APU state struct (VP, GP, EP, DSP core, 256 voices) |
| `apu_regs.h` | MCPX APU register definitions and field masks |
| `apu_core.c` | MMIO handlers, the frame thread, the output, its `waveOut` fallback |
| `apu_vp.c` | Voice Processor — decode, resample, mix, envelopes, HRTF |
| `apu_dsp.c` | The GP and EP, not run: the mixbins passed through |
| `apu_gpfx.c`, `apu_gpfx.h` | The GP's effects settings, followed, and JSRF-PC's own effects run from them |
| `apu_reverb.h`, `apu_hrtf.h` | JSRF-PC's reverb, and the 3D voices placed from their HRTF settings |
| `apu_sdl.c`, `apu_xaudio2.h` | The output, with SDL2's audio, behind the entry points apu_xaudio2.h declares (named for the toolkit's XAudio2 output) |
| `apu_channels.c`, `apu_channels.h` | The two sides of the sound, in the log (Developer > Logging > RECOMP_APU_CHANNELS) |
| `apu_shim.h` | QEMU compatibility (threads, atomics, clocks), on `qemu_shim.h` |
| `qemu_shim.h` | The QEMU types xemu's code is written against (below) |
| `apu_debug.h` | Debug logging macros |
| `fpconv.h` | IEEE 754 float/int conversion utilities |

## Voice Processor Details

The VP processes 256 hardware voices per frame:

- **ADPCM decode**: Xbox ADPCM (4-bit, 64-sample blocks)
- **PCM formats**: 8-bit unsigned, 16-bit signed, 24-bit
- **Pitch**: Log2 fixed-point (4.12 format). Pitch 0 = 48kHz native rate.
- **Envelopes**: Multi-segment amplitude envelopes (attack, decay, sustain, release)
- **HRTF**: Head-Related Transfer Function for 3D positional audio
- **Mixbins**: 32 output channels, voices route to any combination
- **Filters**: Per-voice low-pass/high-pass (IIR biquad)

## Register Map (Key Registers)

```
Offset      Name                   Description
──────────────────────────────────────────────────
0x1000      NV_PAPU_ISTS           Interrupt status
0x1100      NV_PAPU_FECTL          Frontend control
0x2000      NV_PAPU_SECTL          Synth engine control
0x202C      NV_PAPU_VPVADDR        VP voice address base
0x3000+     NV_PAPU_GP*            Global Processor registers
0x4000+     NV_PAPU_EP*            Encode Processor registers
```

Full register definitions in `apu_regs.h`.

## The QEMU shim

xemu's code is written for QEMU's types, and `qemu_shim.h` gives it those,
with C11 equivalents and the Win32 primitives (`src/platform` provides them
on Linux), so the code compiles standalone. `apu_shim.h` includes it.

| QEMU Type | Replacement | Notes |
|-----------|-------------|-------|
| `hwaddr` | `uint64_t` | Hardware address |
| `MemoryRegion` | Struct with size + opaque | MMIO region stub |
| `PCIDevice` | Struct with devfn + config space | PCI device stub |
| `QemuMutex` | Struct holding a `CRITICAL_SECTION` | Win32 mutex |
| `QemuCond` | Struct holding a `CONDITION_VARIABLE` | Win32 condition variable |
| `QemuThread` | Struct holding a `HANDLE` | Win32 thread |
| `qemu_irq` | `int` | Interrupt stub |

Helper macros:
```c
muldiv64(a, b, c)    // 128-bit multiply-divide (uses _umul128 on MSVC)
ctz32(val)            // Count trailing zeros
GET_MASK(v, mask)     // Extract masked field
SET_MASK(v, mask, val)// Set masked field
```

## Dependencies

- src/platform — the Win32 primitives, on POSIX on Linux
- SDL2 — the sound output
- `kernel32` and `winmm`, on Windows
