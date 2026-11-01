# The audio effects

Audio > Audio effects adds our own versions of what the Xbox's audio chip
does to JSRF's sound beyond mixing it: the 3D sounds placed to your left or
right, the characters' voice lines kept on them as they move, the reverb the
game sets for a place, as in the sewers, and a limiter of their own for the
3D sounds. They're made to sound good and cost little, with the console and
xemu as references rather than targets.

It's on by default, and kept as `audioeffects=` in the settings file.
Turned off (`audioeffects=0`), the sound is exactly as before the effects.
Changing it restarts the game, after asking, since the effects are set up as
the game starts (port/audio.c). RECOMP_APU_GPFX=1 or 0 in the environment
wins over the setting.

This page says how the console makes the sound, what the port does with the
effects off, and what each effect does with them on.

## How the console makes JSRF's sound

The Xbox's audio chip, the MCPX APU, works in frames of 32 samples at
48 kHz, 1500 a second, in three parts:

- **The voice processor** plays the game's voices, up to 256, of which 64
  can be 3D, and mixes each into the mixbins: 32 buses, each a frame of
  samples.
- **The GP**, a DSP, runs the game's effects image on the mixbins. JSRF's is
  Media\Sounds\dsstdfx.bin, with 20 effects: first a reverb, then the
  crosstalk that turns the 3D bins into sound for the speakers, then sends
  and mixers.
- **The EP** encodes what the GP leaves for the outputs.

JSRF uses these mixbins:

| Bins | What goes there |
|---|---|
| 0 and 1 | The front left and right: the music, and the title screen's sounds |
| 6 to 9 | The 3D bins, which DirectSound calls the HRTF submixes: front left, front right, back left, back right. The game plays its sound effects and the characters' voice lines as 3D voices into these |
| 10 | The reverb send |

The game's sound library, DirectSound, gives each 3D voice an HRTF setting
as it moves: a filter of 31 taps for each ear, and a delay of up to 42
samples for the far ear. When the game sets a room for a place, DirectSound
works the room out into the reverb's settings and writes them into the GP's
memory.

## What the port does with the effects off

The port's GP is a stub (src/apu/apu_dsp.c), so the effects image doesn't
run. Instead, the port folds the mixbins to stereo itself: bins 0 and 1 as
they are, the 3D bins at 0.35, the back pair a further 3 dB down, and the
reverb send left out. Then an output limiter keeps the result under full
scale. The voice processor has xemu's HRTF filter, which runs with
RECOMP_APU_MIX=xemu, xemu's own mix. In the fold it isn't run, so:

- the 3D sounds come out in the middle;
- there's no reverb, as in the sewers;
- at busy moments the 3D bins add up to well above full scale, 2.7 in one
  player's session, and the output limiter turns the music down with them.

## What the effects do

Each frame, in this order:

1. **The voice processor places each 3D voice** from its HRTF setting
   (src/apu/apu_hrtf.h, called from apu_vp.c), and sends each ear to its own
   side's 3D bins.
2. **Our effects run on the mixbins** (src/apu/apu_gpfx.c, called from
   apu_dsp.c just before the fold): the reverb, from bin 10 into the 3D bins
   (src/apu/apu_reverb.h), then the 3D bins' limiter.
3. **The port's fold and output limiter**, as with the effects off.

And once a game frame, **each character's voice lines are moved to them**
(port/voice_follow.c).

### The effects' settings, followed

This part runs with the effects off too. DirectSound talks to the GP
through the GP's scratch memory in guest RAM: it puts the effects image or
an effect's new settings there and rings a doorbell, and the GP copies them
into its own memory. apu_gpfx.c keeps a copy of the GP's memory
as those commands leave it. It keeps it where the CPU sees the GP's memory
(0xFE830000) too, because DirectSound reads the reverb's state back from
there before it works out a room. The log can then show each effect's
settings, and our own effects can use them, as the GP would.

### Placing the 3D voices

There's no filter. A 3D voice keeps its sound as it is, and takes two
things from its HRTF setting:

- **Each ear's level,** from the size of its taps: the root of the sum of
  their squares. The louder ear stays at full level, so a sound in front is
  as loud as with no setting. The other is turned down by three quarters of
  the setting's difference in dB (`APU_HRTF_DEPTH`).
- **The delay,** on the quieter ear, blended between two samples where it
  falls between them. Where the setting has the delay on the louder ear, it
  goes to the other, and the log says how often.

The filter isn't used because, being 31 taps in 8 bits, it dulls the
sound, and xemu's setter scales both ears' taps to the same size, which
takes away most of the difference in level between them. Running it for
each sample also cost about a third of a core for 32 voices in a test.

Both the levels and the delay glide to each new setting through two
smoothing stages of 25 ms each (`APU_HRTF_GLIDE_MS`): half-way in 42 ms, and
90% of the way in 97 ms. The game sends a moving sound 20 to 30 settings a
second, and an ear's level can step by 10 dB from one to the next, so the
glide carries the sound on between them. xemu's filter glides in about
2 ms. A voice's first frame starts where its setting places it.

The three quarters and the 25 ms were set by ear on speakers, where the
whole difference and a 15 ms glide sounded jarring, and checked on
headphones.

The ears go to the HRTF submixes two at a time, in DirectSound's order:
front left, back left, front right, back right. So the left ear goes to
bins 6 and 8, and the right to 7 and 9. The voice's other bins keep the dry
sound.

### The characters' voice lines, kept on them

The game plays a character's voice line at the character's position, which
the sound manager copies once, and moves only the one looped voice a
character keeps, each frame. So a line came from where the character was
when it began: a skater at full speed left their lines behind.

port/voice_follow.c keeps each line played from a character's voice bank at
that character, and moves it to them once a frame with the same two calls
the game makes for its looped voice. It lets a line go when it stops, or
when the game destroys the character, before it frees the character's
memory. Reading a character after it's freed crashed the game, as at the
end of the police fight that opens Pharaoh Park: the game can give the
memory back, and then it can't be read. The hooks are in
port/recomp_manual.c, on the game's call that plays a sound (sub_00119540),
on the call that ends the sound manager's update (sub_001185B0), and on the
characters' destructor (sub_0007E830).

### The reverb

src/apu/apu_reverb.h builds the reverb as the effects image lays it out,
and runs it from the room's settings in the copy of the GP's memory:

- **The send,** halved and low-passed, then taken down to 24 kHz, where the
  rest runs, 16 samples a frame.
- **Fourteen delay lines,** as long as the image makes them. An input line,
  which every tap of the room reads first. Three lines of build-up, each fed
  from the one before through a low-pass. A feedback comb for each output:
  its early reflections. Four lines of late reverb, mixed by the image's
  matrix, with some of the build-up, and written back through a low-pass.
  And two long loops of the build-up.
- **Each output** is its reflections, its late reverb and a long loop at the
  room's levels, taken back up to 48 kHz and added into its 3D bin. The long
  loops go one to the front pair and one to the back.

In a test, on the sewers' own settings, its outputs matched a model of the
console's reverb to 133 dB, which is float rounding. Where it differs, it's
to sound better:

- **Smoother at the top.** The console keeps every other sample on the way
  down and plays each twice on the way up, which is harsh above 12 kHz.
  Here each way goes through a halfband filter, flat to 9 kHz and 75 dB down
  from 16 kHz.
- **No click when the room changes.** Every level and filter glides each
  sample (all but 1/e of the way in 23 ms, and on the room's after 0.22 s),
  where the console glides once a frame. Each tap that moves fades across to
  where it goes over 5.3 ms, where the console fades it out and back in.
  And the filters keep what they hold, where DirectSound clears them.

It runs only once an image laid out as JSRF's has loaded, and only if its
lines fit the 384 KB kept for them. The sewers need 321 KB. If it can't
run, the log says why, once.

In the stages tested, only the sewers (Stg21 and Stg23) set a room. The
others set a dry one, with every level zero, so the reverb adds nothing
there, though it still runs.

The sewers' late reverb rings long: in a test it fell about 7 dB a second.
That's what DirectSound sends. It works out the late filters as if the late
lines were a quarter of their length, so they decay about four times slower
than the 2.8 s of a sewer-pipe room. The GP leaves the lengths DirectSound
reads as the image has them, so the console should ring as long. That's
from reading the image's code, not from hearing a console.

### The 3D bins' limiter

The 3D bins go through a limiter of their own before the fold, at full
scale, as the console's 24-bit bins stop there. It looks one frame ahead,
so the 3D bins come out a frame (0.67 ms) late, and the gain has come down
by the time a loud frame comes out. It's one gain for the four bins, so a
sound doesn't move between the speakers. The gain glides within each frame
and comes back over 150 ms. The output limiter stays after the fold, as a
safety net.

## The log

At every start, a line says whether the effects are on, and why:
`[AUDIO] effects on (settings)`, or which way RECOMP_APU_GPFX had them.

With Developer > Logging > RECOMP_APU_STATS, every line starts
`[APU] effects:`:

- **With the effects on or off:** a line when an image loads, and one each
  time DirectSound changes an effect's settings, with the words it changed.
- **When each effect starts:** one line each for the limiter, the reverb
  (with its lines' size), the placing and the voice lines. If the reverb
  can't run, one line saying why.
- **When the room changes:** the reverb's new room, as its reflections, late
  reverb and long loops with their delays and levels, or "dry", and whether
  it glides there.
- **Once a second:** how far the 3D bins peaked and how much the limiter
  turned them down; how long the reverb took, and how loud its send and what
  it added were; how many 3D voices were placed, how far down their far ears
  were, what the placing took, and how often and how far the game moved
  them.
- **For each 3D voice placed for 0.3 s or more,** when it stops: how often
  the game moved it, and where it started and ended. **For each voice line
  kept:** how long, and how far its speaker went.

## What it costs

From the test logs, on an ROG Ally X and a Windows PC: the reverb took
1.6 to 3.0 ms of each second on average (0.2 to 0.3% of a core), and
5.2 ms at most, in a second with a 48 ms hitch. The placing took about 1 ms
of each second on average, and 3.4 ms at most. The limiter does a few
multiplications a sample. The plan's limit was about 3% of one Ally X core.

## Tests

All with made-up data, and no game files:

- **tests/apu_gpfx:** the limiter on made-up mixbins; the reverb with a
  made-up room in a made-up image (its reflections, its outputs, the long
  loops, a dry room, the halfband, room changes, other images and lines that
  don't fit); every log line, word for word; nothing with the effects off;
  and the setting passed in, with the environment winning over it.
- **tests/apu_hrtf:** the placing's levels, delays and glide.
- **tests/apu_mix:** a 3D voice placed through the voice processor and the
  fold, with the effects on and off.
- **tests/voice_follow:** voice lines kept on made-up characters.
- **tests/audio:** the setting and its row: on with no `audioeffects=` or
  with 1, off with 0, RECOMP_APU_GPFX winning over it, and the row keeping
  its change and restarting the game.

## Not done

- The image's other effects don't run, its crosstalk among them: with the
  effects on, the placing and the fold do that work.
- macOS, iOS and Android aren't built. The effects are plain C. Android, the
  Mac and the iPhone don't have the in-game menu, so the row isn't there,
  and the effects are on.

docs/upstream.md has an entry for each upstream file the effects change.
