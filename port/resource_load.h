/* The game's resource loads, checked (resource_load.c).
 *
 * One resource manager (task slot 7) loads every kind of resource, one file
 * at a time. Its character loader (resource type 2, sub_00032C70) reads a
 * character's .dat into the manager's file buffer and releases it, but keeps
 * the pointer when the character's voice bank is loaded already. Both
 * character unloads (sub_00032790 and sub_000332F0) release that pointer
 * again. By then the heap may have handed the address to another file, so
 * the second release frees a file in use: on the way out of the garage, the
 * next mission's, which hung the game on Rokkaku-dai Heights. After each
 * step of the loader, the pointer is forgotten once the buffer it names has
 * been released, as the loader's other releases of it do.
 *
 * The stage each mission asks for is logged too, with what its file said
 * when it loaded, so a mission file written over shows where.
 * The hooks in port/recomp_manual.c call these. */
#ifndef JSRF_RESOURCE_LOAD_H
#define JSRF_RESOURCE_LOAD_H

#include <stdint.h>

/* Nonzero when no live allocation holds guest address `va`: it was released. */
typedef int (*resource_load_released_fn)(uint32_t va);

/* After a step of the character loader on the resource manager at `mgr`.
 * `mem` is guest memory, `t` the game's time in seconds, for the log. */
void resource_load_char_step(uint8_t *mem, uint32_t mgr, resource_load_released_fn released,
                             double t);

/* The flow at `flow`'s state, read before a step that asks for its stage
 * (sub_0004AC80, sub_0004ACB0). */
uint32_t resource_load_flow_state(const uint8_t *mem, uint32_t flow);

/* After that step, with the state before it: logs the request once the step
 * has made it. */
void resource_load_stage_step(const uint8_t *mem, uint32_t flow, uint32_t state_before, double t);

#endif
