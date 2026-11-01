/* The characters' voice lines kept on them as they move (voice_follow.c).
 *
 * The game plays a character's voice line (sub_00119540: the character's
 * voice bank, the line, and where) at where the character is, and the sound
 * manager copies that position once (sub_00118A60). The one voice a
 * character keeps in +0xC80, a looped one, its update moves each frame:
 * sub_00118710 says whether it still plays, and sub_00118BD0 moves it to
 * the character (sub_00080340). Every other line stays where it began, so a
 * skater at full speed leaves it behind, and with the free camera at an
 * angle it comes from one side. With Audio > Audio effects
 * (RECOMP_APU_GPFX) on, every line played at its speaker is moved the same
 * way, once a game frame, until it ends. Off, nothing changes.
 * The hooks in port/recomp_manual.c make the game's calls. */
#ifndef JSRF_VOICE_FOLLOW_H
#define JSRF_VOICE_FOLLOW_H

#include <stdint.h>

/* The game's own calls: whether the sound with this handle still plays
 * (sub_00118710), and moving it to the position at guest address `where`
 * (sub_00118BD0). */
typedef int (*voice_follow_playing_fn)(uint32_t handle);
typedef void (*voice_follow_move_fn)(uint32_t handle, uint32_t where);

/* The game played a sound (sub_00119540) from `bank`, at the position at
 * guest address `where` (0: none), and got `handle` back. `mem` is guest
 * memory, `ram` its size, and `esp` the caller's stack. Kept if it is a
 * character's voice line, played where that character is. */
void voice_follow_played(const uint8_t *mem, uint32_t ram, uint32_t handle, uint32_t bank,
                         uint32_t where, uint32_t esp);

/* Once a game frame, as the sound manager hands the frame's settings to the
 * sound engine (sub_001185B0): each line kept is moved to its speaker, and
 * let go once it has ended. */
void voice_follow_frame(const uint8_t *mem, voice_follow_playing_fn playing,
                        voice_follow_move_fn move);

#endif
