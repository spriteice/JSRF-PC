/*
 * MCPX APU Voice Processor - Standalone extraction from xemu
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "apu_state.h"
#include <string.h>
#include "fpconv.h"
#include "apu_channels.h"
#include "apu_gpfx.h"
#include "apu_hrtf.h"
#include "platform/recomp_check.h"

/* #define DEBUG_MCPX */

#ifdef DEBUG_MCPX
#define DPRINTF(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do { } while (0)
#endif

/* ============================================================
 * Voice list register table
 * ============================================================ */

static const struct {
    hwaddr top, current, next;
} voice_list_regs[] = {
    { NV_PAPU_TVL2D, NV_PAPU_CVL2D, NV_PAPU_NVL2D }, /* 2D */
    { NV_PAPU_TVL3D, NV_PAPU_CVL3D, NV_PAPU_NVL3D }, /* 3D */
    { NV_PAPU_TVLMP, NV_PAPU_CVLMP, NV_PAPU_NVLMP }, /* MP */
};

/* ============================================================
 * Notify status helper
 * ============================================================ */

/* How often the voice processor has told the title something finished, and
 * how many voices it has turned off. Counted because "DirectSound never
 * retires the voice" has two halves -- the APU not saying so, or the title
 * not hearing it -- and no amount of reading either side's code distinguishes
 * them. Reported by the freeze watchdog. */
unsigned long g_apu_notifies;
unsigned long g_apu_voice_offs;
/* What the title has asked the audio hardware to do, in total. */
unsigned g_apu_fe_on, g_apu_fe_off, g_apu_fe_cfg, g_apu_fe_other, g_apu_fe_total;
/* The streaming path, counted separately from everything else.
 *
 * On a console the interrupt that drains DirectSound's pending-stop queue is
 * raised by ANY voice notification, and a title streaming music raises one
 * every buffer segment -- hundreds a second. Here a whole run raises one to
 * four. If stream voices are never reaching the end of a segment, that is the
 * reason the queue never drains and the reason the music decays, and those
 * are the same bug seen from two sides. These four counters say which step
 * stops: voices seen in stream format, segments completed, and each of the
 * two places a stream voice can report itself done. */
unsigned long g_apu_stream_voices, g_apu_seg_done, g_apu_notify_ssl, g_apu_notify_persist;
unsigned long g_apu_voices_seen, g_apu_voices_paused;

static void set_notify_status(MCPXAPUState *d, uint32_t v, int notifier,
                              int status)
{
    g_apu_notifies++;
    hwaddr notify_offset = d->regs[NV_PAPU_FENADDR];
    notify_offset += 16 * (MCPX_HW_NOTIFIER_BASE_OFFSET +
                           v * MCPX_HW_NOTIFIER_COUNT + notifier);
    notify_offset += 15;

    stb_phys(address_space_memory, notify_offset, (uint8_t)status);
    stb_phys(address_space_memory, notify_offset - 1, 1);

    qatomic_or(&d->regs[NV_PAPU_ISTS],
               NV_PAPU_ISTS_FEVINTSTS | NV_PAPU_ISTS_FENINTSTS);
    d->set_irq = true;
}

/* ============================================================
 * Filter helpers
 * ============================================================ */

/* Audio > Audio effects (RECOMP_APU_GPFX): the 3D voices' ears, as
 * apu_hrtf.h places them. */
static ApuHrtfEars s_ears[MCPX_HW_MAX_3D_VOICES];

static void voice_reset_filters(MCPXAPUState *d, uint16_t v)
{
    if (!RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v))
        return;
    memset(&d->vp.filters[v].svf, 0, sizeof(d->vp.filters[v].svf));
    hrtf_filter_clear_history(&d->vp.filters[v].hrtf);
    /* A 3D voice starting again starts where its setting places it. */
    if (v < MCPX_HW_MAX_3D_VOICES)
        s_ears[v].started = 0;
    if (d->vp.filters[v].resampler) {
        src_reset(d->vp.filters[v].resampler);
    }
    /* A voice starting again starts its resampler again: the two source
     * frames it was interpolating between belong to whatever it was playing
     * before. */
    d->vp.filters[v].rs_primed = 0;
    d->vp.filters[v].rs_phase = 0.0f;
    d->vp.filters[v].rs_have = 0;
    d->vp.filters[v].rs_pos = 0;
}

static bool voice_should_mute(uint16_t v)
{
    bool m = (g_dbg_voice_monitor >= 0) && (v != g_dbg_voice_monitor);
    return m || mcpx_apu_debug_is_muted(v);
}

/* ============================================================
 * Utility functions
 * ============================================================ */

static float clampf(float v, float mn, float mx)
{
    if (v < mn) return mn;
    if (v > mx) return mx;
    return v;
}

static float attenuate(uint16_t vol)
{
    vol &= 0xFFF;
    return (vol == 0xFFF) ? 0.0f : powf(10.0f, vol / (64.0f * -20.0f));
}

/* ============================================================
 * Voice register accessors (read/write voice struct in RAM)
 * ============================================================ */

static uint32_t voice_get_mask(MCPXAPUState *d, uint16_t voice_handle,
                               hwaddr offset, uint32_t mask)
{
    hwaddr voice = d->regs[NV_PAPU_VPVADDR] + voice_handle * NV_PAVS_SIZE;
    return (ldl_le_phys(address_space_memory, voice + offset) & mask) >>
           ctz32(mask);
}

static void voice_set_mask(MCPXAPUState *d, uint16_t voice_handle,
                           hwaddr offset, uint32_t mask, uint32_t val)
{
    hwaddr voice = d->regs[NV_PAPU_VPVADDR] + voice_handle * NV_PAVS_SIZE;
    uint32_t v = ldl_le_phys(address_space_memory, voice + offset) & ~mask;
    stl_le_phys(address_space_memory, voice + offset,
                v | ((val << ctz32(mask)) & mask));
}

/* ============================================================
 * Voice off / lock
 * ============================================================ */

static void voice_off(MCPXAPUState *d, uint16_t v)
{
    g_apu_voice_offs++;
    voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                   NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 0);

    bool stream = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT,
                                 NV_PAVS_VOICE_CFG_FMT_DATA_TYPE) != 0;
    int notifier = MCPX_HW_NOTIFIER_SSLA_DONE;
    if (stream && RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v)
               && RECOMP_CHECK((unsigned)d->vp.ssl[v].ssl_index <= 1,
                               "voice %u's list %d", v, d->vp.ssl[v].ssl_index))
        notifier += d->vp.ssl[v].ssl_index;
    set_notify_status(d, v, notifier, NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
}

static void voice_lock(MCPXAPUState *d, uint16_t v, bool lock)
{
    if (!RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v))
        return;
    qemu_mutex_lock(&d->lock);

    uint64_t mask = 1ULL << (v % 64);
    if (lock) {
        d->vp.voice_locked[v / 64] |= mask;
    } else {
        d->vp.voice_locked[v / 64] &= ~mask;
    }

    qemu_cond_signal(&d->cond);
    qemu_mutex_unlock(&d->lock);
}

static bool is_voice_locked(MCPXAPUState *d, uint16_t v)
{
    if (!RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v))
        return false;
    uint64_t mask = 1ULL << (v % 64);
    return (qatomic_read(&d->vp.voice_locked[v / 64]) & mask) != 0;
}

/* ============================================================
 * HRIR coefficient setter
 * ============================================================ */

static void set_hrir_coeff_tar(MCPXAPUState *d, int channel, int coeff_idx,
                               int8_t value)
{
    int entry = d->vp.hrtf.current_entry;
    d->vp.hrtf.entries[entry].hrir[channel][coeff_idx] = int8_to_float(value);
}

/* ============================================================
 * The front end's trap, the stop wait, and the sound's watch
 *
 * For b3's baseline build: lines only, nothing here changes what the voice
 * processor or the front end does.
 *
 * RECOMP_APU_STATS, every ten seconds from the frame thread:
 *
 *   - the trap (row 12b's G1): how many times an idle voice trapped the
 *     front end, how many of the title's methods ran while it was trapped,
 *     how often the title read FEDECPARAM while trapped and how many of
 *     those reads weren't the voice the trap was for (a method run in the
 *     trap overwrites it, fe_method), and how many reports each idle voice
 *     took before it left its list (upstream's #340: the first idle voice
 *     left in a list takes every report);
 *   - the stop wait (row 12b's check 1, the Load Game hang): for each voice
 *     the title turns off, the time from its VOICE_OFF to the first frame
 *     that finds it out of its list, which is what DirectSound's stop wait
 *     (sub_001A308E, inlined in sub_001A3E58) spins on.
 *
 * RECOMP_APU_WATCH, the sound post-mortem's three observations (lesson 2):
 *
 *   - each change of a playing voice's BA, LBO, CBO or EBO between two
 *     frames that the voice processor didn't make, said as made by a method
 *     or in memory with no method: the voice array's words read each frame;
 *   - for line B (an ADPCM mono buffer voice started with EBO 12351, whose
 *     line ends at sample 11647, 6552 bytes in), what follows it in memory,
 *     at VOICE_ON and as the voice passes 11647: its peak and loudness
 *     decoded and a hash of the bytes, not the bytes, so no game data is in
 *     the log;
 *   - when the game looks at line B's position after it passes 11647 (a
 *     VOICE_LOCK with nothing changed under it, as GetCurrentPosition reads
 *     CBO) and when it stops it, in frames and milliseconds after the pass.
 * ============================================================ */

extern double host_info_seconds(void) __attribute__((weak));
static double watch_seconds(void) { return host_info_seconds ? host_info_seconds() : 0.0; }

static int s_vp_stats = -1, s_vp_watch = -1;   /* set by mcpx_apu_vp_init */
static uint32_t s_vp_frame;   /* frames since the start, from 1; the frame thread writes it */

static struct {
    unsigned long traps, trapped_methods, reads, reads_other;
    unsigned long off_retired, off_restarted;
    unsigned long long off_us, off_longest_us;
} s_g1;
static uint32_t s_trap_voice;                  /* the voice the last trap was for */
static int64_t s_off_us[MCPX_HW_MAX_VOICES];   /* the title's VOICE_OFF, when; 0 none */

/* The frame thread's alone: each idle voice's reports, and the idle voices
 * in a list on the last frame and this one. */
static uint32_t s_reports[MCPX_HW_MAX_VOICES];
static uint32_t s_idle_seen[MCPX_HW_MAX_VOICES], s_active_seen[MCPX_HW_MAX_VOICES];
static uint16_t s_idle_last[3 * MCPX_HW_MAX_VOICES], s_idle_now[3 * MCPX_HW_MAX_VOICES];
static int s_idle_last_n, s_idle_now_n;
static unsigned long s_left[4], s_left_restarted;   /* after 0, 1, 2, 3+ reports */
static uint32_t s_left_most;

static int vp_trapped(MCPXAPUState *d)
{
    return (qatomic_read(&d->regs[NV_PAPU_FECTL]) & NV_PAPU_FECTL_FEMETHMODE)
           == NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
}

/* The title read FEDECPARAM (apu_core.c's mcpx_apu_read). */
void mcpx_apu_vp_fedecparam_read(MCPXAPUState *d, uint32_t value);
void mcpx_apu_vp_fedecparam_read(MCPXAPUState *d, uint32_t value)
{
    if (s_vp_stats <= 0 || !vp_trapped(d)) return;
    __atomic_fetch_add(&s_g1.reads, 1, __ATOMIC_RELAXED);
    if (value != __atomic_load_n(&s_trap_voice, __ATOMIC_RELAXED))
        __atomic_fetch_add(&s_g1.reads_other, 1, __ATOMIC_RELAXED);
}

/* A frame's walk of the lists: voice v idle in one, reported or not. */
static void g1_idle(uint16_t v, int reported)
{
    if (s_vp_stats <= 0 || v >= MCPX_HW_MAX_VOICES) return;
    if (s_idle_seen[v] != s_vp_frame && s_idle_now_n < 3 * MCPX_HW_MAX_VOICES)
        s_idle_now[s_idle_now_n++] = v;
    s_idle_seen[v] = s_vp_frame;
    if (reported) s_reports[v]++;
}

/* After the walk: the idle voices of the last frame no longer in a list
 * have left it, or were started again. */
static void g1_walked(void)
{
    int i;
    if (s_vp_stats <= 0) return;
    for (i = 0; i < s_idle_last_n; i++) {
        uint16_t v = s_idle_last[i];
        int64_t off;
        if (s_idle_seen[v] == s_vp_frame) continue;
        if (s_active_seen[v] == s_vp_frame) {
            s_left_restarted++;
        } else {
            uint32_t r = s_reports[v];
            s_left[r < 3 ? r : 3]++;
            if (r > s_left_most) s_left_most = r;
            off = __atomic_exchange_n(&s_off_us[v], 0, __ATOMIC_RELAXED);
            if (off) {
                int64_t us = qemu_clock_get_us(QEMU_CLOCK_REALTIME) - off;
                if (us < 0) us = 0;
                __atomic_fetch_add(&s_g1.off_retired, 1, __ATOMIC_RELAXED);
                __atomic_fetch_add(&s_g1.off_us, (unsigned long long)us, __ATOMIC_RELAXED);
                if ((unsigned long long)us > __atomic_load_n(&s_g1.off_longest_us, __ATOMIC_RELAXED))
                    __atomic_store_n(&s_g1.off_longest_us, (unsigned long long)us, __ATOMIC_RELAXED);
            }
        }
        s_reports[v] = 0;
    }
    memcpy(s_idle_last, s_idle_now, (size_t)s_idle_now_n * sizeof s_idle_now[0]);
    s_idle_last_n = s_idle_now_n;
    s_idle_now_n = 0;
}

/* A method of the title's (mcpx_apu_vp_write, the title's thread). */
static void g1_method(MCPXAPUState *d, uint32_t method, uint32_t argument)
{
    uint32_t v;
    if (s_vp_stats <= 0) return;
    if (vp_trapped(d)) __atomic_fetch_add(&s_g1.trapped_methods, 1, __ATOMIC_RELAXED);
    if (method == NV1BA0_PIO_VOICE_OFF) {
        int64_t none = 0;
        v = argument & NV1BA0_PIO_VOICE_OFF_HANDLE;
        if (v < MCPX_HW_MAX_VOICES)
            __atomic_compare_exchange_n(&s_off_us[v], &none, qemu_clock_get_us(QEMU_CLOCK_REALTIME),
                                        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    } else if (method == NV1BA0_PIO_VOICE_ON) {
        v = argument & NV1BA0_PIO_VOICE_ON_HANDLE;
        if (v < MCPX_HW_MAX_VOICES && __atomic_exchange_n(&s_off_us[v], 0, __ATOMIC_RELAXED))
            __atomic_fetch_add(&s_g1.off_restarted, 1, __ATOMIC_RELAXED);
    }
}

/* The two lines, every ten seconds, from the frame thread (tests/apu_trap_lines
 * builds this file with every frame's). */
#ifndef APU_G1_REPORT_MS
#define APU_G1_REPORT_MS 10000
#endif
static void g1_report(void)
{
    static int64_t last_ms;
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    unsigned long n;
    unsigned long long us;
    if (s_vp_stats <= 0) return;
    if (!last_ms) last_ms = now;
    if (now - last_ms < APU_G1_REPORT_MS) return;
    last_ms = now;
    fprintf(stderr, "[APU] the front end, the last 10 s: %lu traps for an idle voice, %lu of "
            "the title's methods run while one was set; FEDECPARAM read %lu times while "
            "trapped, %lu of them not the voice it trapped for; %lu idle voices out of their "
            "lists, after 1 report %lu, 2 %lu, 3 or more %lu (the most %u), none %lu; %lu "
            "started again first\n",
            __atomic_exchange_n(&s_g1.traps, 0, __ATOMIC_RELAXED),
            __atomic_exchange_n(&s_g1.trapped_methods, 0, __ATOMIC_RELAXED),
            __atomic_exchange_n(&s_g1.reads, 0, __ATOMIC_RELAXED),
            __atomic_exchange_n(&s_g1.reads_other, 0, __ATOMIC_RELAXED),
            s_left[0] + s_left[1] + s_left[2] + s_left[3], s_left[1], s_left[2], s_left[3],
            s_left_most, s_left[0], s_left_restarted);
    memset(s_left, 0, sizeof s_left);
    s_left_most = 0;
    s_left_restarted = 0;
    n = __atomic_exchange_n(&s_g1.off_retired, 0, __ATOMIC_RELAXED);
    us = __atomic_exchange_n(&s_g1.off_us, 0, __ATOMIC_RELAXED);
    fprintf(stderr, "[APU] the title's VOICE_OFFs, the last 10 s: %lu out of their lists, "
            "the longest %.1f ms from VOICE_OFF to out of its list, %.2f ms on average "
            "(what DirectSound's stop wait spins on); %lu started again before a frame "
            "found them out\n",
            n, (double)__atomic_exchange_n(&s_g1.off_longest_us, 0, __ATOMIC_RELAXED) / 1000.0,
            n ? (double)us / 1000.0 / (double)n : 0.0,
            __atomic_exchange_n(&s_g1.off_restarted, 0, __ATOMIC_RELAXED));
    fflush(stderr);
}

/* ---- RECOMP_APU_WATCH ---- */

#define WATCH_LINES     2000        /* the change lines' cap */
#define LINE_B_EBO      12351u      /* line B's EBO at VOICE_ON: the whole buffer */
#define LINE_B_LAST     11647u      /* its line's last sample */
#define LINE_B_BYTES    6552u       /* the bytes its line takes: 182 ADPCM blocks */

enum { W_BA = 1, W_LBO = 2, W_CBO = 4, W_EBO = 8 };
static const char *const k_watch_word[4] = { "BA", "LBO", "CBO", "EBO" };
static uint8_t s_w_method[MCPX_HW_MAX_VOICES];   /* W_ bits a method wrote; atomic */
static uint8_t s_w_on[MCPX_HW_MAX_VOICES];       /* VOICE_ON since the last frame; atomic */
static struct { uint32_t w[4], on_frame; int valid; } s_w[MCPX_HW_MAX_VOICES];
static int s_w_lines;

static struct {
    int voice;                     /* -1 none; atomic */
    uint32_t pass_frame;           /* the frame it passed LINE_B_LAST, 0 not yet; atomic */
    int64_t pass_us;               /* atomic */
    uint32_t look_frame, look_cbo; /* the first look after the pass; atomic */
    int64_t look_us;
    int locked, between;           /* a VOICE_LOCK on it, and methods since; atomic */
} s_lb = { -1, 0, 0, 0, 0, 0, 0, 0 };

static hwaddr get_data_ptr(hwaddr sge_base, unsigned int max_sge, uint32_t addr);

static void watch_words(MCPXAPUState *d, uint16_t v, uint32_t w[4])
{
    w[0] = voice_get_mask(d, v, NV_PAVS_VOICE_CUR_PSL_START, NV_PAVS_VOICE_CUR_PSL_START_BA);
    w[1] = voice_get_mask(d, v, NV_PAVS_VOICE_CUR_PSH_SAMPLE, NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO);
    w[2] = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO);
    w[3] = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EBO);
}

/* What follows line B in memory, to its buffer's end: decoded peak and
 * loudness, and an FNV-1a hash of the bytes. */
static void watch_after_line_b(MCPXAPUState *d, uint16_t v, uint32_t ba, uint32_t ebo,
                               const char *when)
{
    uint32_t end = (ebo / ADPCM_SAMPLES_PER_BLOCK + 1) * 36u, off, hash = 2166136261u;
    uint32_t block[9];
    int16_t pcm[65 * 2];
    double sum = 0.0;
    int peak = 0, any = 0, samples = 0, i;
    for (off = LINE_B_BYTES; off + 36 <= end; off += 36) {
        for (i = 0; i < 9; i++) {
            hwaddr a = get_data_ptr(d->regs[NV_PAPU_VPSGEADDR], 0xFFFFFFFF, ba + off + 4u * (uint32_t)i);
            block[i] = ldl_le_phys(address_space_memory, a);
        }
        for (i = 0; i < 36; i++) {
            uint8_t b = ((const uint8_t *)block)[i];
            hash = (hash ^ b) * 16777619u;
            any |= b;
        }
        adpcm_decode_block(pcm, (const uint8_t *)block, 36, 1);
        for (i = 0; i < ADPCM_SAMPLES_PER_BLOCK; i++) {
            int a = pcm[i] < 0 ? -pcm[i] : pcm[i];
            if (a > peak) peak = a;
            sum += (double)pcm[i] * pcm[i];
            samples++;
        }
    }
    fprintf(stderr, "[APUWATCH] t=%.3fs line B (voice %u, ba 0x%06X) %s: the %u bytes after "
            "its line (from ba + %u): peak %d, rms %.0f decoded, hash %08X%s\n",
            watch_seconds(), v, ba, when, end > LINE_B_BYTES ? end - LINE_B_BYTES : 0,
            LINE_B_BYTES, peak, samples ? sqrt(sum / samples) : 0.0, hash,
            any ? "" : ", all zero");
}

/* A method on a voice (fe_method, the title's thread). */
static void watch_method(MCPXAPUState *d, uint32_t method, uint32_t argument)
{
    uint32_t v = d->regs[NV_PAPU_FECV];
    int bit = 0;
    if (s_vp_watch <= 0) return;
    switch (method) {
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_BASE: bit = W_BA; break;
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_LBO:  bit = W_LBO; break;
    case NV1BA0_PIO_SET_VOICE_BUF_CBO:      bit = W_CBO; break;
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_EBO:  bit = W_EBO; break;
    case NV1BA0_PIO_VOICE_ON:
        v = argument & NV1BA0_PIO_VOICE_ON_HANDLE;
        if (v < MCPX_HW_MAX_VOICES) __atomic_store_n(&s_w_on[v], 1, __ATOMIC_RELAXED);
        return;
    case NV1BA0_PIO_SET_CURRENT_VOICE:
        return;
    case NV1BA0_PIO_VOICE_LOCK:
        if ((int)v != __atomic_load_n(&s_lb.voice, __ATOMIC_RELAXED)) return;
        if (argument & 1) {
            __atomic_store_n(&s_lb.between, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.locked, 1, __ATOMIC_RELAXED);
        } else if (__atomic_exchange_n(&s_lb.locked, 0, __ATOMIC_RELAXED)
                   && !__atomic_load_n(&s_lb.between, __ATOMIC_RELAXED)
                   && __atomic_load_n(&s_lb.pass_frame, __ATOMIC_RELAXED)
                   && !__atomic_load_n(&s_lb.look_frame, __ATOMIC_RELAXED)) {
            /* A look at it with nothing changed: the first since the pass. */
            __atomic_store_n(&s_lb.look_cbo, voice_get_mask(d, (uint16_t)v,
                             NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO),
                             __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.look_us, qemu_clock_get_us(QEMU_CLOCK_REALTIME), __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.look_frame, __atomic_load_n(&s_vp_frame, __ATOMIC_RELAXED),
                             __ATOMIC_RELAXED);
        }
        return;
    case NV1BA0_PIO_VOICE_OFF: {
        uint32_t off = argument & NV1BA0_PIO_VOICE_OFF_HANDLE, pass, look, now;
        int64_t us = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        if ((int)off != __atomic_load_n(&s_lb.voice, __ATOMIC_RELAXED)) break;
        now = __atomic_load_n(&s_vp_frame, __ATOMIC_RELAXED);
        pass = __atomic_load_n(&s_lb.pass_frame, __ATOMIC_RELAXED);
        look = __atomic_load_n(&s_lb.look_frame, __ATOMIC_RELAXED);
        if (!pass)
            fprintf(stderr, "[APUWATCH] t=%.3fs line B (voice %u) off at sample %u, before it "
                    "passed sample %u\n", watch_seconds(), off,
                    voice_get_mask(d, (uint16_t)off, NV_PAVS_VOICE_PAR_OFFSET,
                                   NV_PAVS_VOICE_PAR_OFFSET_CBO), LINE_B_LAST);
        else if (!look)
            fprintf(stderr, "[APUWATCH] t=%.3fs line B (voice %u) off %u frames (%.1f ms) after "
                    "it passed sample %u, with no look at it between\n", watch_seconds(), off,
                    now - pass, (double)(us - __atomic_load_n(&s_lb.pass_us, __ATOMIC_RELAXED)) / 1000.0,
                    LINE_B_LAST);
        else
            fprintf(stderr, "[APUWATCH] t=%.3fs line B (voice %u) off %u frames (%.1f ms) after "
                    "it passed sample %u; the game's first look at it after that (a VOICE_LOCK "
                    "with nothing changed) %u frames (%.1f ms) after, at sample %u\n",
                    watch_seconds(), off, now - pass,
                    (double)(us - __atomic_load_n(&s_lb.pass_us, __ATOMIC_RELAXED)) / 1000.0,
                    LINE_B_LAST, look - pass,
                    (double)(__atomic_load_n(&s_lb.look_us, __ATOMIC_RELAXED)
                             - __atomic_load_n(&s_lb.pass_us, __ATOMIC_RELAXED)) / 1000.0,
                    __atomic_load_n(&s_lb.look_cbo, __ATOMIC_RELAXED));
        __atomic_store_n(&s_lb.voice, -1, __ATOMIC_RELAXED);
        break;
    }
    default:
        break;
    }
    if (__atomic_load_n(&s_lb.locked, __ATOMIC_RELAXED))
        __atomic_fetch_add(&s_lb.between, 1, __ATOMIC_RELAXED);
    if (bit && v < MCPX_HW_MAX_VOICES)
        __atomic_fetch_or(&s_w_method[v], (uint8_t)bit, __ATOMIC_RELAXED);
}

/* A playing voice, before the frame mixes it: what changed since the last. */
static void watch_before(MCPXAPUState *d, uint16_t v)
{
    uint32_t w[4];
    int k, m;
    if (s_vp_watch <= 0 || v >= MCPX_HW_MAX_VOICES) return;
    watch_words(d, v, w);
    m = __atomic_exchange_n(&s_w_method[v], 0, __ATOMIC_RELAXED);
    if (__atomic_exchange_n(&s_w_on[v], 0, __ATOMIC_RELAXED)) {
        uint32_t fmt = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT, 0xFFFFFFFF);
        s_w[v].valid = 0;
        s_w[v].on_frame = s_vp_frame;
        if (w[3] == LINE_B_EBO && !(fmt & NV_PAVS_VOICE_CFG_FMT_DATA_TYPE)
            && !(fmt & NV_PAVS_VOICE_CFG_FMT_STEREO)
            && GET_MASK(fmt, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE)
               == NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM) {
            __atomic_store_n(&s_lb.pass_frame, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.look_frame, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.locked, 0, __ATOMIC_RELAXED);
            __atomic_store_n(&s_lb.voice, (int)v, __ATOMIC_RELAXED);
            watch_after_line_b(d, v, w[0], w[3], "on");
        }
    }
    if (!s_w[v].valid) return;
    for (k = 0; k < 4; k++) {
        if (w[k] == s_w[v].w[k] || s_w_lines >= WATCH_LINES) continue;
        if (++s_w_lines == WATCH_LINES)
            fprintf(stderr, "[APUWATCH] the change lines' cap of %d is reached: no more\n",
                    WATCH_LINES);
        fprintf(stderr, "[APUWATCH] t=%.3fs voice %u's %s %u -> %u, %s, %u frames after "
                "VOICE_ON\n", watch_seconds(), v, k_watch_word[k], s_w[v].w[k], w[k],
                (m & (1 << k)) ? "by a method" : "in memory, by no method",
                s_vp_frame - s_w[v].on_frame);
    }
}

/* ... and after it: what the frame left, and line B passing its end. */
static void watch_after(MCPXAPUState *d, uint16_t v)
{
    uint32_t before;
    if (s_vp_watch <= 0 || v >= MCPX_HW_MAX_VOICES) return;
    before = s_w[v].w[2];
    watch_words(d, v, s_w[v].w);
    if (!s_w[v].valid) { s_w[v].valid = 1; before = 0; }
    if ((int)v == __atomic_load_n(&s_lb.voice, __ATOMIC_RELAXED)
        && !__atomic_load_n(&s_lb.pass_frame, __ATOMIC_RELAXED)
        && before <= LINE_B_LAST && s_w[v].w[2] > LINE_B_LAST) {
        __atomic_store_n(&s_lb.pass_us, qemu_clock_get_us(QEMU_CLOCK_REALTIME), __ATOMIC_RELAXED);
        __atomic_store_n(&s_lb.pass_frame, s_vp_frame, __ATOMIC_RELAXED);
        watch_after_line_b(d, v, s_w[v].w[0], s_w[v].w[3], "passing its end");
    }
}

/* ============================================================
 * Front-End method dispatch
 * ============================================================ */

static void fe_method(MCPXAPUState *d, uint32_t method, uint32_t argument)
{
    /* RECOMP_APU_TRACE=1: what the title is asking the audio hardware to do.
     *
     * "There is no sound" splits at this line. If VOICE_ON never appears, the
     * title never asked for a sound and the bug is upstream of the hardware
     * entirely; if it appears and the mix is still silent, the bug is in this
     * file. Counted rather than printed per call -- DirectSound issues these
     * by the thousand. */
    {
        static int trace = -1;
        static int64_t last_ms;
#define n_on    g_apu_fe_on
#define n_off   g_apu_fe_off
#define n_cfg   g_apu_fe_cfg
#define n_other g_apu_fe_other
#define n_total g_apu_fe_total
        if (trace < 0) trace = getenv("RECOMP_APU_TRACE") ? 1 : 0;
        /* Counted unconditionally now, not only under the trace switch.
         *
         * "The front end stopped being called" and "the front end is being
         * called and the title is not asking for anything" are different
         * failures with the same silence, and the freeze report needs to tell
         * them apart without a second run under a different environment. The
         * counting is four increments on a path that already does real work. */
        {
            int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
            n_total++;
            if (method == NV1BA0_PIO_VOICE_ON) n_on++;
            else if (method == NV1BA0_PIO_VOICE_OFF) n_off++;
            else if (method >= NV1BA0_PIO_SET_VOICE_CFG_VBIN
                  && method < NV1BA0_PIO_SET_VOICE_CFG_VBIN + 0x100) n_cfg++;
            else n_other++;
            if (!last_ms) last_ms = now;
            if (trace && now - last_ms >= 2000) {
                fprintf(stderr, "  [APU-FE] %u methods: VOICE_ON %u, VOICE_OFF "
                        "%u, voice cfg %u, other %u\n",
                        n_total, n_on, n_off, n_cfg, n_other);
                fflush(stderr);
                last_ms = now;
            }
        }
#undef n_on
#undef n_off
#undef n_cfg
#undef n_other
#undef n_total
    }

    unsigned int slot;

    d->regs[NV_PAPU_FEDECMETH] = method;
    d->regs[NV_PAPU_FEDECPARAM] = argument;
    unsigned int selected_handle, list;

    switch (method) {
    case NV1BA0_PIO_VOICE_LOCK:
        voice_lock(d, (uint16_t)d->regs[NV_PAPU_FECV], argument & 1);
        break;

    case NV1BA0_PIO_SET_ANTECEDENT_VOICE:
        d->regs[NV_PAPU_FEAV] = argument;
        break;

    case NV1BA0_PIO_VOICE_ON: {
        selected_handle = argument & NV1BA0_PIO_VOICE_ON_HANDLE;
        /* The handle is 16 bits and indexes the voices' own state below
         * (ssl[], the filters): a voice past the last can't be started. */
        if (!RECOMP_CHECK(selected_handle < MCPX_HW_MAX_VOICES, "voice %u", selected_handle))
            break;
        /* What format each voice is started in.
         *
         * The voice processor ran 378,000 voice-frames in a hundred seconds
         * and reported NOT ONE in stream format, which is why the music never
         * advances a buffer segment and why CRI stops asking for sectors after
         * the first few. Either the title never starts a streaming voice, or
         * DATA_TYPE is not being read from where the title wrote it. Printing
         * the format word at the moment the voice is switched on settles
         * which, and there are only a handful of these per run. */
        { uint32_t fmt = voice_get_mask(d, (uint16_t)selected_handle,
                                        NV_PAVS_VOICE_CFG_FMT, 0xFFFFFFFF);
          fprintf(stderr, "  [VOICE] on %u: CFG_FMT=0x%08X data_type=%u "
                          "(%s) loop=%u\n",
                  selected_handle, fmt,
                  (unsigned)voice_get_mask(d, (uint16_t)selected_handle,
                        NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_DATA_TYPE),
                  voice_get_mask(d, (uint16_t)selected_handle,
                        NV_PAVS_VOICE_CFG_FMT,
                        NV_PAVS_VOICE_CFG_FMT_DATA_TYPE) ? "STREAM" : "buffer",
                  (unsigned)voice_get_mask(d, (uint16_t)selected_handle,
                        NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_LOOP));
          fflush(stderr); }

        bool locked = is_voice_locked(d, (uint16_t)selected_handle);
        if (!locked) voice_lock(d, (uint16_t)selected_handle, true);

        list = GET_MASK(d->regs[NV_PAPU_FEAV], NV_PAPU_FEAV_LST);
        if (list != NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_INHERIT) {
            unsigned int top_reg = voice_list_regs[list - 1].top;
            voice_set_mask(d, (uint16_t)selected_handle,
                           NV_PAVS_VOICE_TAR_PITCH_LINK,
                           NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                           d->regs[top_reg]);
            d->regs[top_reg] = selected_handle;
        } else {
            unsigned int antecedent_voice =
                GET_MASK(d->regs[NV_PAPU_FEAV], NV_PAPU_FEAV_VALUE);

            /* No voice to follow: linking after "none" would write the
             * link 0xFFFF voices past the voice array, in the game's memory. */
            if (RECOMP_CHECK(antecedent_voice != 0xFFFF, "voice %u", selected_handle)) {
                uint32_t next_handle = voice_get_mask(
                    d, (uint16_t)antecedent_voice, NV_PAVS_VOICE_TAR_PITCH_LINK,
                    NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);
                voice_set_mask(d, (uint16_t)selected_handle,
                               NV_PAVS_VOICE_TAR_PITCH_LINK,
                               NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                               next_handle);
                voice_set_mask(d, (uint16_t)antecedent_voice,
                               NV_PAVS_VOICE_TAR_PITCH_LINK,
                               NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,
                               selected_handle);
            }
        }

        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_OFFSET,
                       NV_PAVS_VOICE_PAR_OFFSET_CBO, 0);
        d->vp.ssl[selected_handle].ssl_seg = 0;
        d->vp.ssl[selected_handle].ssl_index = 0;

        unsigned int ea_start = GET_MASK(argument, NV1BA0_PIO_VOICE_ON_ENVA);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EACUR, ea_start);
        if (ea_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_DELAY) {
            uint16_t delay_time =
                (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
                    NV_PAVS_VOICE_CFG_ENV0, NV_PAVS_VOICE_CFG_ENV0_EA_DELAYTIME);
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EACOUNT, delay_time * 16);
        } else if (ea_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_ATTACK) {
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EACOUNT, 0);
        } else if (ea_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_HOLD) {
            uint16_t hold_time =
                (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
                    NV_PAVS_VOICE_CFG_ENVA, NV_PAVS_VOICE_CFG_ENVA_EA_HOLDTIME);
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EACOUNT, hold_time * 16);
        }

        unsigned int ef_start = GET_MASK(argument, NV1BA0_PIO_VOICE_ON_ENVF);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EFCUR, ef_start);
        if (ef_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_DELAY) {
            uint16_t delay_time =
                (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
                    NV_PAVS_VOICE_CFG_ENV1, NV_PAVS_VOICE_CFG_ENV0_EA_DELAYTIME);
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EFCOUNT, delay_time * 16);
        } else if (ef_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_ATTACK) {
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EFCOUNT, 0);
        } else if (ef_start == NV_PAVS_VOICE_PAR_STATE_EFCUR_HOLD) {
            uint16_t hold_time =
                (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
                    NV_PAVS_VOICE_CFG_ENVF, NV_PAVS_VOICE_CFG_ENVA_EA_HOLDTIME);
            voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                           NV_PAVS_VOICE_CUR_ECNT_EFCOUNT, hold_time * 16);
        }

        voice_reset_filters(d, (uint16_t)selected_handle);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 1);

        if (!locked) voice_lock(d, (uint16_t)selected_handle, false);
        break;
    }

    case NV1BA0_PIO_VOICE_RELEASE: {
        selected_handle = argument & NV1BA0_PIO_VOICE_ON_HANDLE;

        bool locked = is_voice_locked(d, (uint16_t)selected_handle);
        if (!locked) voice_lock(d, (uint16_t)selected_handle, true);

        uint16_t rr;
        rr = (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
            NV_PAVS_VOICE_TAR_LFO_ENV, NV_PAVS_VOICE_TAR_LFO_ENV_EA_RELEASERATE);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                       NV_PAVS_VOICE_CUR_ECNT_EACOUNT, rr * 16);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EACUR,
                       NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);

        rr = (uint16_t)voice_get_mask(d, (uint16_t)selected_handle,
            NV_PAVS_VOICE_CFG_MISC, NV_PAVS_VOICE_CFG_MISC_EF_RELEASERATE);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_CUR_ECNT,
                       NV_PAVS_VOICE_CUR_ECNT_EFCOUNT, rr * 16);
        voice_set_mask(d, (uint16_t)selected_handle, NV_PAVS_VOICE_PAR_STATE,
                       NV_PAVS_VOICE_PAR_STATE_EFCUR,
                       NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE);

        if (!locked) voice_lock(d, (uint16_t)selected_handle, false);
        break;
    }

    case NV1BA0_PIO_VOICE_OFF:
        voice_off(d, (uint16_t)(argument & NV1BA0_PIO_VOICE_OFF_HANDLE));
        break;

    case NV1BA0_PIO_VOICE_PAUSE:
        voice_set_mask(d, (uint16_t)(argument & NV1BA0_PIO_VOICE_PAUSE_HANDLE),
                       NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_PAUSED,
                       (argument & NV1BA0_PIO_VOICE_PAUSE_ACTION) != 0);
        break;

    case NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY:
        d->vp.hrtf.current_entry =
            GET_MASK(argument, NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY_HANDLE);
        break;

    case NV1BA0_PIO_SET_CURRENT_VOICE:
        d->regs[NV_PAPU_FECV] = argument;
        break;

    case NV1BA0_PIO_SET_VOICE_CFG_VBIN:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_VBIN, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_FMT:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_FMT, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_ENV0:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_ENV0, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_ENVA:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_ENVA, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_ENV1:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_ENV1, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_ENVF:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_ENVF, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_MISC:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CFG_MISC, 0xFFFFFFFF, argument);
        break;

    case NV1BA0_PIO_SET_VOICE_TAR_HRTF: {
        int handle = GET_MASK(argument, NV1BA0_PIO_SET_VOICE_TAR_HRTF_HANDLE);
        unsigned int current_voice = d->regs[NV_PAPU_FECV];
        voice_set_mask(d, (uint16_t)current_voice,
                       NV_PAVS_VOICE_CFG_HRTF_TARGET,
                       NV_PAVS_VOICE_CFG_HRTF_TARGET_HANDLE, handle);
        if (current_voice < MCPX_HW_MAX_3D_VOICES &&
            handle != HRTF_NULL_HANDLE &&
            RECOMP_CHECK(handle < HRTF_ENTRY_COUNT, "voice %u's HRTF entry %d",
                         current_voice, handle)) {
            hrtf_filter_set_target_params(&d->vp.filters[current_voice].hrtf,
                                          d->vp.hrtf.entries[handle].hrir,
                                          d->vp.hrtf.entries[handle].itd);
        }
        break;
    }

    case NV1BA0_PIO_SET_VOICE_TAR_VOLA:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_VOLA, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_TAR_VOLB:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_VOLB, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_TAR_VOLC:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_VOLC, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_LFO_ENV:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_LFO_ENV, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_TAR_FCA:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_FCA, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_TAR_FCB:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_FCB, 0xFFFFFFFF, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_TAR_PITCH:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_TAR_PITCH_LINK,
                       NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH,
                       (argument & NV1BA0_PIO_SET_VOICE_TAR_PITCH_STEP) >> 16);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_BASE:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CUR_PSL_START,
                       NV_PAVS_VOICE_CUR_PSL_START_BA, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_LBO:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_CUR_PSH_SAMPLE,
                       NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_BUF_CBO:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_PAR_OFFSET,
                       NV_PAVS_VOICE_PAR_OFFSET_CBO, argument);
        break;
    case NV1BA0_PIO_SET_VOICE_CFG_BUF_EBO:
        voice_set_mask(d, (uint16_t)d->regs[NV_PAPU_FECV],
                       NV_PAVS_VOICE_PAR_NEXT,
                       NV_PAVS_VOICE_PAR_NEXT_EBO, argument);
        break;

    case NV1BA0_PIO_SET_CURRENT_INBUF_SGE:
        d->vp.inbuf_sge_handle = argument & NV1BA0_PIO_SET_CURRENT_INBUF_SGE_HANDLE;
        break;

    case NV1BA0_PIO_SET_CURRENT_INBUF_SGE_OFFSET: {
        hwaddr sge_address =
            d->regs[NV_PAPU_VPSGEADDR] + d->vp.inbuf_sge_handle * 8;
        stl_le_phys(address_space_memory, sge_address,
                    argument & NV1BA0_PIO_SET_CURRENT_INBUF_SGE_OFFSET_PARAMETER);
        break;
    }

    case NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE:
        d->vp.outbuf_sge_handle =
            argument & NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE_HANDLE;
        break;

    case NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE_OFFSET: {
        hwaddr sge_address =
            d->regs[NV_PAPU_VPSGEADDR] + d->vp.outbuf_sge_handle * 8;
        stl_le_phys(address_space_memory, sge_address,
                    argument & NV1BA0_PIO_SET_CURRENT_OUTBUF_SGE_OFFSET_PARAMETER);
        break;
    }

    case NV1BA0_PIO_SET_VOICE_SSL_A: {
        int ssl = 0;
        unsigned int current_voice = d->regs[NV_PAPU_FECV];
        if (!RECOMP_CHECK(current_voice < MCPX_HW_MAX_VOICES, "voice %u", current_voice))
            break;
        d->vp.ssl[current_voice].base[ssl] =
            GET_MASK(argument, NV1BA0_PIO_SET_VOICE_SSL_A_BASE);
        d->vp.ssl[current_voice].count[ssl] =
            (uint8_t)GET_MASK(argument, NV1BA0_PIO_SET_VOICE_SSL_A_COUNT);
        break;
    }
    case NV1BA0_PIO_SET_VOICE_SSL_B: {
        int ssl = 1;
        unsigned int current_voice = d->regs[NV_PAPU_FECV];
        if (!RECOMP_CHECK(current_voice < MCPX_HW_MAX_VOICES, "voice %u", current_voice))
            break;
        d->vp.ssl[current_voice].base[ssl] =
            GET_MASK(argument, NV1BA0_PIO_SET_VOICE_SSL_A_BASE);
        d->vp.ssl[current_voice].count[ssl] =
            (uint8_t)GET_MASK(argument, NV1BA0_PIO_SET_VOICE_SSL_A_COUNT);
        break;
    }

    case NV1BA0_PIO_SET_CURRENT_SSL: {
        /* Only the game's memory is reached through it (the SSL segment
         * writes below), so the console's way is kept: said, then taken. */
        RECOMP_CHECK((argument & 0x3f) == 0, "list base 0x%X", argument);
        RECOMP_CHECK(argument < (MCPX_HW_MAX_SSL_PRDS * NV_PSGE_SIZE), "list base 0x%X", argument);
        d->vp.ssl_base_page = argument;
        break;
    }

    case NV1BA0_PIO_SET_HRTF_SUBMIXES:
        d->vp.hrtf_submix[0] = (uint8_t)((argument >> 0) & 0x1f);
        d->vp.hrtf_submix[1] = (uint8_t)((argument >> 8) & 0x1f);
        d->vp.hrtf_submix[2] = (uint8_t)((argument >> 16) & 0x1f);
        d->vp.hrtf_submix[3] = (uint8_t)((argument >> 24) & 0x1f);
        break;

    case NV1BA0_PIO_SET_HRTF_HEADROOM:
        d->vp.hrtf_headroom = (uint8_t)(argument & NV1BA0_PIO_SET_HRTF_HEADROOM_AMOUNT);
        break;

    case SE2FE_IDLE_VOICE:
        if (d->regs[NV_PAPU_FETFORCE1] & NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE) {
            d->regs[NV_PAPU_FECTL] &= ~NV_PAPU_FECTL_FEMETHMODE;
            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
            d->regs[NV_PAPU_FECTL] &= ~NV_PAPU_FECTL_FETRAPREASON;
            d->regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FETRAPREASON_REQUESTED;
            d->set_irq = true;
            if (s_vp_stats > 0) {
                __atomic_fetch_add(&s_g1.traps, 1, __ATOMIC_RELAXED);
                __atomic_store_n(&s_trap_voice, argument, __ATOMIC_RELAXED);
            }
        }
        break;

    default:
        /* Handle range-based cases that can't use case ranges in MSVC */
        if (method >= NV1BA0_PIO_SET_HRIR && method < NV1BA0_PIO_SET_HRIR_X) {
            if (!RECOMP_CHECK((unsigned)d->vp.hrtf.current_entry < HRTF_ENTRY_COUNT,
                              "HRTF entry %d", d->vp.hrtf.current_entry))
                break;
            slot = (method - NV1BA0_PIO_SET_HRIR) / 4;
            int8_t left0 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_LEFT0);
            int8_t right0 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_RIGHT0);
            int8_t left1 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_LEFT1);
            int8_t right1 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_RIGHT1);
            int coeff_idx = slot * 2;
            set_hrir_coeff_tar(d, 0, coeff_idx, left0);
            set_hrir_coeff_tar(d, 1, coeff_idx, right0);
            set_hrir_coeff_tar(d, 0, coeff_idx + 1, left1);
            set_hrir_coeff_tar(d, 1, coeff_idx + 1, right1);
        } else if (method == NV1BA0_PIO_SET_HRIR_X) {
            if (!RECOMP_CHECK((unsigned)d->vp.hrtf.current_entry < HRTF_ENTRY_COUNT,
                              "HRTF entry %d", d->vp.hrtf.current_entry))
                break;
            int8_t left30 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_X_LEFT30);
            int8_t right30 = (int8_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_X_RIGHT30);
            int16_t itd = (int16_t)GET_MASK(argument, NV1BA0_PIO_SET_HRIR_X_ITD);
            set_hrir_coeff_tar(d, 0, 30, left30);
            set_hrir_coeff_tar(d, 1, 30, right30);
            d->vp.hrtf.entries[d->vp.hrtf.current_entry].itd = s6p9_to_float(itd);
        } else if (method >= NV1BA0_PIO_SET_SSL_SEGMENT_OFFSET &&
                   method < NV1BA0_PIO_SET_SSL_SEGMENT_LENGTH + 8 * 64) {
            RECOMP_CHECK((method & 0x3) == 0, "method 0x%X", method);
            hwaddr addr = d->regs[NV_PAPU_VPSSLADDR]
                          + (d->vp.ssl_base_page * 8)
                          + (method - NV1BA0_PIO_SET_SSL_SEGMENT_OFFSET);
            stl_le_phys(address_space_memory, addr, argument);
        } else if (method >= NV1BA0_PIO_SET_SUBMIX_HEADROOM &&
                   method <= NV1BA0_PIO_SET_SUBMIX_HEADROOM + 4 * (NUM_MIXBINS - 1)) {
            RECOMP_CHECK((method & 3) == 0, "method 0x%X", method);
            slot = (method - NV1BA0_PIO_SET_SUBMIX_HEADROOM) / 4;
            d->vp.submix_headroom[slot] =
                (uint8_t)(argument & NV1BA0_PIO_SET_SUBMIX_HEADROOM_AMOUNT);
        } else if ((method >= NV1BA0_PIO_SET_OUTBUF_BA &&
                    method < NV1BA0_PIO_SET_OUTBUF_BA + 32) ||
                   (method >= NV1BA0_PIO_SET_OUTBUF_LEN &&
                    method < NV1BA0_PIO_SET_OUTBUF_LEN + 32)) {
            /* Outbuf base/length - ignore for now */
        } else {
            /* Unknown method - silently ignore */
            DPRINTF("Unknown FE method: 0x%08X arg=0x%08X\n", method, argument);
        }
        break;
    }
}

/* ============================================================
 * VP MMIO read/write (exposed to apu_core.c)
 * ============================================================ */

uint64_t mcpx_apu_vp_read(void *opaque, hwaddr addr, unsigned int size)
{
    (void)opaque; (void)size;

    switch (addr) {
    case NV1BA0_PIO_FREE:
        /* Free space in the voice-processor method FIFO. The XDK's
         * DirectSound polls this before every batch of methods, in some
         * thirty tight loops, each waiting for ">= N dwords free", and
         * some of those N are computed from voice counts. Methods here are
         * executed synchronously, so the queue really is always empty:
         * answer with a size no batch can exceed rather than 0x80, which
         * left a batch needing more than 32 dwords spinning forever with
         * the guest lock held. */
        return 0x1000;
    default:
        break;
    }
    return 0;
}

void mcpx_apu_vp_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned int size)
{
    MCPXAPUState *d = (MCPXAPUState *)opaque;
    (void)size;

    /* The trap's and the watch's notes, before the method changes anything. */
    g1_method(d, (uint32_t)addr, (uint32_t)val);
    watch_method(d, (uint32_t)addr, (uint32_t)val);
    /* Dispatch known methods through fe_method */
    fe_method(d, (uint32_t)addr, (uint32_t)val);
}

/* ============================================================
 * SGE data pointer resolution
 * ============================================================ */

static hwaddr get_data_ptr(hwaddr sge_base, unsigned int max_sge, uint32_t addr)
{
    unsigned int entry = addr / TARGET_PAGE_SIZE;
    uint32_t prd_address;
    /* The table is in the game's memory: past its end is read as the
     * console reads it, once said. */
    RECOMP_CHECK(entry <= max_sge, "page %u of %u", entry, max_sge);
    prd_address = ldl_le_phys(address_space_memory, sge_base + entry * 4 * 2);

    /*
     * RECOMP_APU_ADDR=1: what a voice's buffer address actually translates to.
     *
     * A voice's base address is virtual and goes through this scatter-gather
     * table. If the table is empty -- because the base register was never set,
     * or the title filled it somewhere this emulation does not see -- then
     * every lookup returns zero and the voice plays whatever is at physical
     * zero. That is not silence: it is the kernel data area, which has enough
     * structure in it to come out as a constant-amplitude buzz with a short
     * repeating period. Which is exactly what this title's one active voice
     * sounds like.
     */
    { static int on = -1, said;
      if (on < 0) on = getenv("RECOMP_APU_ADDR") ? 1 : 0;
      if (on && said < 8) {
          said++;
          fprintf(stderr, "  [APU-ADDR] sge table at %08X, entry %u -> "
                  "%08X, virtual %08X -> physical %08X%s\n",
                  (unsigned)sge_base, entry, prd_address, addr,
                  (unsigned)(prd_address + addr % TARGET_PAGE_SIZE),
                  prd_address ? "" : "   <- the table is empty here");
          fflush(stderr);
      } }
    return prd_address + addr % TARGET_PAGE_SIZE;
}

/* ============================================================
 * Envelope processing
 * ============================================================ */

static float voice_step_envelope(MCPXAPUState *d, uint16_t v, uint32_t reg_0,
                           uint32_t reg_a, uint32_t rr_reg, uint32_t rr_mask,
                           uint32_t lvl_reg, uint32_t lvl_mask,
                           uint32_t count_mask, uint32_t cur_mask)
{
    uint8_t cur = (uint8_t)voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask);
    switch (cur) {
    case NV_PAVS_VOICE_PAR_STATE_EFCUR_OFF:
        voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, 0);
        voice_set_mask(d, v, lvl_reg, lvl_mask, 0xFF);
        return 1.0f;

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_DELAY: {
        uint16_t count =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask);
        voice_set_mask(d, v, lvl_reg, lvl_mask, 0x00);
        if (count == 0) {
            cur++;
            voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask, cur);
        } else {
            count--;
        }
        voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, count);
        return 0.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_ATTACK: {
        uint16_t count =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask);
        uint16_t attack_rate =
            (uint16_t)voice_get_mask(d, v, reg_0, NV_PAVS_VOICE_CFG_ENV0_EA_ATTACKRATE);
        float value;
        if (attack_rate == 0) {
            value = 255.0f;
        } else {
            if (count <= (uint32_t)(attack_rate * 16)) {
                value = (count * 0xFF) / (float)(attack_rate * 16);
            } else {
                value = 255.0f;
            }
        }
        voice_set_mask(d, v, lvl_reg, lvl_mask, (uint32_t)value);
        if (count == (uint32_t)(attack_rate * 16)) {
            cur++;
            voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask, cur);
            uint16_t hold_time =
                (uint16_t)voice_get_mask(d, v, reg_a, NV_PAVS_VOICE_CFG_ENVA_EA_HOLDTIME);
            count = hold_time * 16;
        } else {
            count++;
        }
        voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, count);
        return value / 255.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_HOLD: {
        uint16_t count =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask);
        voice_set_mask(d, v, lvl_reg, lvl_mask, 0xFF);
        if (count == 0) {
            cur++;
            voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask, cur);
            uint16_t decay_rate =
                (uint16_t)voice_get_mask(d, v, reg_a, NV_PAVS_VOICE_CFG_ENVA_EA_DECAYRATE);
            count = decay_rate * 16;
        } else {
            count--;
        }
        voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, count);
        return 1.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_DECAY: {
        uint16_t count =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask);
        uint16_t decay_rate =
            (uint16_t)voice_get_mask(d, v, reg_a, NV_PAVS_VOICE_CFG_ENVA_EA_DECAYRATE);
        uint8_t sustain_level =
            (uint8_t)voice_get_mask(d, v, reg_a, NV_PAVS_VOICE_CFG_ENVA_EA_SUSTAINLEVEL);
        float value;
        if (decay_rate == 0) {
            value = 0.0f;
        } else {
            value = 255.0f * powf(0.99988799f, (decay_rate * 16 - count) *
                                                   4096.0f / decay_rate);
        }
        if (value <= (sustain_level + 0.2f) || (value > 255.0f)) {
            cur++;
            voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask, cur);
        } else {
            count--;
            voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, count);
            voice_set_mask(d, v, lvl_reg, lvl_mask, (uint32_t)value);
        }
        return value / 255.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_SUSTAIN: {
        uint8_t sustain_level =
            (uint8_t)voice_get_mask(d, v, reg_a, NV_PAVS_VOICE_CFG_ENVA_EA_SUSTAINLEVEL);
        voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, 0x00);
        voice_set_mask(d, v, lvl_reg, lvl_mask, sustain_level);
        return sustain_level / 255.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE: {
        uint16_t count =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask);
        uint16_t release_rate = (uint16_t)voice_get_mask(d, v, rr_reg, rr_mask);
        if (release_rate == 0) count = 0;
        float value = 0;
        if (count == 0) {
            voice_set_mask(d, v, NV_PAVS_VOICE_PAR_STATE, cur_mask, ++cur);
        } else {
            float pos = clampf(1 - count / (release_rate * 16.0f), 0, 1);
            uint8_t lvl = (uint8_t)voice_get_mask(d, v, lvl_reg, lvl_mask);
            value = powf((float)M_E, -6.91f * pos) * lvl;
            count--;
            voice_set_mask(d, v, NV_PAVS_VOICE_CUR_ECNT, count_mask, count);
        }
        return value / 255.0f;
    }

    case NV_PAVS_VOICE_PAR_STATE_EFCUR_FORCE_RELEASE:
        if (count_mask == NV_PAVS_VOICE_CUR_ECNT_EACOUNT) {
            voice_off(d, v);
        }
        return 0.0f;

    default:
        fprintf(stderr, "[APU] Unknown envelope state 0x%x\n", cur);
        return 0.0f;
    }
}

/* ============================================================
 * Sample fetching from voice buffers
 * ============================================================ */

static int voice_get_samples(MCPXAPUState *d, uint32_t v, float samples[][2],
                       int num_samples_requested)
{
    if (!RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v))
        return -1;
    bool stereo = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                                 NV_PAVS_VOICE_CFG_FMT_STEREO) != 0;
    unsigned int channels = stereo ? 2 : 1;
    unsigned int sample_size = voice_get_mask(
        d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE);
    unsigned int container_sizes[4] = { 1, 2, 0, 4 };
    unsigned int container_size_index = voice_get_mask(
        d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE);
    unsigned int container_size = container_sizes[container_size_index];
    bool stream = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                                 NV_PAVS_VOICE_CFG_FMT_DATA_TYPE) != 0;
    bool paused = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_STATE,
                                 NV_PAVS_VOICE_PAR_STATE_PAUSED) != 0;

    /* Which voices the processor actually sees, and what state they are in.
     *
     * "0 segments finished" says a stream voice never reaches the end of its
     * buffer, and there are three quite different reasons that could be true:
     * no voice is in stream format at all, stream voices exist but are paused,
     * or they run and their offsets never advance. Counting the voices as they
     * are processed separates those without another build. */
    g_apu_voices_seen++;
    if (stream) g_apu_stream_voices++;
    if (paused) g_apu_voices_paused++;
    bool loop = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                               NV_PAVS_VOICE_CFG_FMT_LOOP) != 0;
    uint32_t ebo = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_NEXT,
                                  NV_PAVS_VOICE_PAR_NEXT_EBO);
    uint32_t cbo = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_OFFSET,
                                  NV_PAVS_VOICE_PAR_OFFSET_CBO);
    uint32_t lbo = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CUR_PSH_SAMPLE,
                                  NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO);
    uint32_t ba = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CUR_PSL_START,
                                 NV_PAVS_VOICE_CUR_PSL_START_BA);
    unsigned int samples_per_block =
        1 + voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                           NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK);
    bool persist = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                                  NV_PAVS_VOICE_CFG_FMT_PERSIST) != 0;

    int ssl_index = 0, ssl_seg = 0, page = 0, count = 0;
    int seg_len = 0, seg_cs = 0, seg_spb = 0, seg_s = 0;
    hwaddr segment_offset = 0;
    uint32_t segment_length = 0;
    size_t block_size;

    int adpcm_block_index = -1;
    uint32_t adpcm_block[36 * 2 / 4];
    int16_t adpcm_decoded[65 * 2];

    /* RECOMP_APU_VOICE=1: what this voice is pointed at.
     *
     * A voice that decodes silence has either been given no data or been given
     * an address this decoder reads wrongly, and the two look the same from
     * the mix. The base address plus a peak of the bytes actually there tells
     * them apart in one line. */
    {
        static int trace = -1;
        static int64_t last_ms[MCPX_HW_MAX_VOICES];
        if (trace < 0) trace = getenv("RECOMP_APU_VOICE") ? 1 : 0;
        if (trace && v < MCPX_HW_MAX_VOICES) {
            int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
            if (now - last_ms[v] >= 1000) {
                const uint8_t *base = apu_phys_ptr(ba);
                int k, pk = 0;
                for (k = 0; k < 256; k++) {
                    int a = (int16_t)((uint16_t)base[2 * k]
                          | ((uint16_t)base[2 * k + 1] << 8));
                    if (a < 0) a = -a;
                    if (a > pk) pk = a;
                }
                fprintf(stderr, "  [VOICE %3u] ba=%08X cbo=%u ebo=%u %s%s"
                        "  bytes-at-ba peak %d  fmt: %s %s %uch spb=%u%s\n",
                        v, (unsigned)ba, cbo, ebo,
                        stream ? "stream" : "buffer", paused ? " PAUSED" : "",
                        pk,
                        /* The three fields that decide how the bytes at ba are
                         * read. A voice decoded in the wrong container size or
                         * the wrong sample type produces full-scale noise at a
                         * constant amplitude, which is exactly what this one
                         * produces -- 737 distinct values in 8192 samples,
                         * repeating every 256. */
                        (voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                                        NV_PAVS_VOICE_CFG_FMT_DATA_TYPE)
                         ? "adpcm" : "pcm"),
                        (const char *[]){ "8-bit", "16-bit", "adpcm-container", "32-bit" }
                          [voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_CFG_FMT,
                                          NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE) & 3],
                        (unsigned)(1 + voice_get_mask(d, (uint16_t)v,
                                        NV_PAVS_VOICE_CFG_FMT,
                                        NV_PAVS_VOICE_CFG_FMT_MULTIPASS)),
                        samples_per_block,
                        loop ? " loop" : "");
                fflush(stderr);
                last_ms[v] = now;
            }
        }
    }

    voice_set_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_STATE,
                   NV_PAVS_VOICE_PAR_STATE_NEW_VOICE, 0);

    if (paused) return -1;

    if (stream) {
        if (!persist) {
            int eacur = voice_get_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_STATE,
                                       NV_PAVS_VOICE_PAR_STATE_EACUR);
            if (eacur < NV_PAVS_VOICE_PAR_STATE_EFCUR_RELEASE) {
                voice_off(d, (uint16_t)v);
                return -1;
            }
        }

        RECOMP_CHECK(!loop, "voice %u", v);
        ssl_index = d->vp.ssl[v].ssl_index;
        ssl_seg = d->vp.ssl[v].ssl_seg;
        page = d->vp.ssl[v].base[ssl_index] + ssl_seg;
        count = d->vp.ssl[v].count[ssl_index];

        if (count == 0) {
            voice_set_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_OFFSET,
                           NV_PAVS_VOICE_PAR_OFFSET_CBO, 0);
            d->vp.ssl[v].ssl_seg = 0;
            if (!persist) {
                d->vp.ssl[v].ssl_index = 0;
                voice_off(d, (uint16_t)v);
            } else {
                g_apu_notify_persist++;
                set_notify_status(d, v, MCPX_HW_NOTIFIER_SSLA_DONE +
                                  d->vp.ssl[v].ssl_index,
                                  NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
            }
            return -1;
        }

        hwaddr addr = d->regs[NV_PAPU_VPSSLADDR] + page * 8;
        segment_offset = ldl_le_phys(address_space_memory, addr);
        segment_length = ldl_le_phys(address_space_memory, addr + 4);
        RECOMP_CHECK(segment_offset != 0, "voice %u, list page %d", v, page);
        RECOMP_CHECK(segment_length != 0, "voice %u, list page %d", v, page);
        seg_len = (segment_length >> 0) & 0xffff;
        seg_cs = (segment_length >> 16) & 3;
        seg_spb = (segment_length >> 18) & 0x1f;
        seg_s = (segment_length >> 23) & 1;
        container_size_index = seg_cs;
        if (seg_cs == NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM) {
            sample_size = NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S24;
        }
        RECOMP_CHECK(seg_len > 0, "voice %u, list page %d", v, page);
        ebo = seg_len - 1;
    }

    bool adpcm =
        (container_size_index == NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);

    if (adpcm) {
        block_size = 36;
    } else {
        block_size = container_size;
    }
    block_size *= samples_per_block;

    int sample_count = 0;
    for (; (sample_count < num_samples_requested) && (cbo <= ebo);
         sample_count++, cbo++) {
        if (adpcm) {
            unsigned int block_index = cbo / ADPCM_SAMPLES_PER_BLOCK;
            unsigned int block_position = cbo % ADPCM_SAMPLES_PER_BLOCK;
            if (adpcm_block_index != (int)block_index) {
                uint32_t linear_addr = block_index * (uint32_t)block_size;
                if (stream) {
                    hwaddr addr = segment_offset + linear_addr;
                    memcpy(adpcm_block, apu_phys_ptr(addr), block_size);
                } else {
                    linear_addr += ba;
                    for (unsigned int word_index = 0;
                         word_index < (9 * samples_per_block); word_index++) {
                        hwaddr addr = get_data_ptr(d->regs[NV_PAPU_VPSGEADDR],
                                                   0xFFFFFFFF, linear_addr);
                        adpcm_block[word_index] =
                            ldl_le_phys(address_space_memory, addr);
                        linear_addr += 4;
                    }
                }
                adpcm_decode_block(adpcm_decoded, (uint8_t *)adpcm_block,
                                   block_size, channels);
                adpcm_block_index = block_index;
            }

            samples[sample_count][0] =
                int16_to_float(adpcm_decoded[block_position * channels]);
            if (stereo) {
                samples[sample_count][1] = int16_to_float(
                    adpcm_decoded[block_position * channels + 1]);
            }
        } else {
            hwaddr addr;
            /* RECOMP_APU_SRC=<file>: the bytes this voice is reading, once.
             *
             * The mixed output is noise, the address translation is correct,
             * and the voice's format says plain 16-bit PCM. That leaves two
             * possibilities that look identical from the speakers: the title
             * wrote nonsense into its ring buffer -- which would make this a
             * recompilation bug in the title's own audio decoder rather than
             * anything to do with the emulated APU -- or it wrote audio and
             * this reads it with the wrong stride. Dumping what is actually
             * there separates them: audio is smooth from one sample to the
             * next, and nothing else is. */
            { static int done, looked; static const char *pth;
              /* Look the variable up once: this runs for every voice on
               * every audio frame, and a getenv per call was six percent of
               * the audio thread with nothing set. */
              if (!looked) { looked = 1; pth = getenv("RECOMP_APU_SRC"); }
              if (!done) {
                  if (pth && !stream && ebo > 4096) {
                      done = 1;
                      FILE *f = fopen(pth, "wb");
                      if (f) {
                          uint32_t q;
                          for (q = 0; q < 65536; q += 2) {
                              hwaddr a = get_data_ptr(d->regs[NV_PAPU_VPSGEADDR],
                                                      0xFFFFFFFF, ba + q);
                              uint16_t w = (uint16_t)lduw_le_phys(
                                               address_space_memory, a);
                              fwrite(&w, 2, 1, f);
                          }
                          fclose(f);
                          fprintf(stderr, "  [APU] wrote 64 KB of voice %u's "
                                  "buffer (ba=%08X, %u-byte container, "
                                  "stride %u) to %s\n", v, (unsigned)ba,
                                  (unsigned)container_size,
                                  (unsigned)block_size, pth);
                      }
                  }
              } }
            if (stream) {
                addr = segment_offset + cbo * block_size;
            } else {
                uint32_t linear_addr = ba + cbo * (uint32_t)block_size;
                addr = get_data_ptr(d->regs[NV_PAPU_VPSGEADDR], 0xFFFFFFFF,
                                    linear_addr);
            }

            for (unsigned int channel = 0; channel < channels; channel++) {
                uint32_t ival;
                float fval;
                switch (sample_size) {
                case NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_U8:
                    ival = ldub_phys(address_space_memory, addr);
                    fval = uint8_to_float((uint8_t)(ival & 0xff));
                    break;
                case NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16:
                    ival = lduw_le_phys(address_space_memory, addr);
                    fval = int16_to_float((int16_t)(ival & 0xffff));
                    break;
                case NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S24:
                    ival = ldl_le_phys(address_space_memory, addr);
                    fval = int24_to_float(ival);
                    break;
                case NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S32:
                    ival = ldl_le_phys(address_space_memory, addr);
                    fval = int32_to_float(ival);
                    break;
                default:
                    fval = 0.0f;
                    break;
                }
                samples[sample_count][channel] = fval;
                addr += container_size;
            }
        }

        if (!stereo) {
            samples[sample_count][1] = samples[sample_count][0];
        }
    }

    if (cbo >= ebo) {
        if (stream) {
            g_apu_seg_done++;
            d->vp.ssl[v].ssl_seg += 1;
            cbo = 0;
            if (d->vp.ssl[v].ssl_seg < d->vp.ssl[v].count[ssl_index]) {
                /* Move to next segment */
            } else {
                int next_index = (ssl_index + 1) % 2;
                d->vp.ssl[v].ssl_index = next_index;
                d->vp.ssl[v].ssl_seg = 0;
                g_apu_notify_ssl++;
                set_notify_status(d, v, MCPX_HW_NOTIFIER_SSLA_DONE + ssl_index,
                                  NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS);
            }
        } else {
            if (loop) {
                cbo = lbo;
            } else {
                cbo = ebo;
                voice_off(d, (uint16_t)v);
            }
        }
    }

    voice_set_mask(d, (uint16_t)v, NV_PAVS_VOICE_PAR_OFFSET,
                   NV_PAVS_VOICE_PAR_OFFSET_CBO, cbo);
    return sample_count;
}

/* ============================================================
 * Voice resampling.
 *
 * A voice carries its own sample rate as a pitch register, and the APU runs
 * at 48 kHz: NV_PAVS_VOICE_TAR_PITCH_LINK gives the ratio of the two, which
 * voice_process turns into `rate` -- output frames per source frame, exactly
 * libsamplerate's src_ratio, which is what xemu hands its sinc resampler.
 *
 * This used to ignore the rate entirely and hand back source samples one for
 * one, so every voice played at 48 kHz whatever rate it was recorded at.
 * JSRF's are 22.05 and 32 kHz: a 22.05 kHz voice came out 2.18x too fast and
 * an octave and a fifth too high, with everything the resampler would have
 * interpolated away turning into hiss. Measured on the title screen, a fifth
 * of the output's energy sat above 8 kHz and its spectral flatness was 0.3 --
 * the shape of noise, not of music.
 *
 * Linear interpolation between the two source frames the output sample falls
 * between. xemu uses a sinc resampler and notes in the same breath that the
 * hardware may well be linear; linear costs nothing here and is a world away
 * from no resampling at all.
 * ============================================================ */

/* The next source frame, refilling from the voice in chunks. Returns 0 when
 * the voice has run out -- looping voices never do. */
static int rs_next_frame(MCPXAPUState *d, uint16_t v,
                         MCPXAPUVoiceFilter *filter, float out[2])
{
    if (filter->rs_pos >= filter->rs_have) {
        filter->rs_have = 0;
        filter->rs_pos = 0;
        while (filter->rs_have < RS_CHUNK) {
            int active = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                        NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
            int n;
            if (!active) break;
            n = voice_get_samples(d, v, &filter->rs_buf[filter->rs_have],
                                  RS_CHUNK - filter->rs_have);
            if (n <= 0) break;
            filter->rs_have += n;
        }
        if (filter->rs_have <= 0) return 0;
    }
    out[0] = filter->rs_buf[filter->rs_pos][0];
    out[1] = filter->rs_buf[filter->rs_pos][1];
    filter->rs_pos++;
    return 1;
}

static int voice_resample(MCPXAPUState *d, uint16_t v, float samples[][2],
                          int requested_num, float rate)
{
    MCPXAPUVoiceFilter *filter = &d->vp.filters[v];
    float step;
    int i;

    /* Source frames consumed per output frame. Guarded because the pitch
     * register is the title's to write and a silly value must not spin here. */
    step = (rate > 1.0e-4f) ? 1.0f / rate : 1.0f;
    if (step > 64.0f) step = 64.0f;
    if (step < 1.0f / 64.0f) step = 1.0f / 64.0f;

    if (!filter->rs_primed) {
        if (!rs_next_frame(d, v, filter, filter->rs_prev)) return -1;
        if (!rs_next_frame(d, v, filter, filter->rs_cur)) {
            filter->rs_cur[0] = filter->rs_prev[0];
            filter->rs_cur[1] = filter->rs_prev[1];
        }
        filter->rs_phase = 0.0f;
        filter->rs_primed = 1;
    }

    for (i = 0; i < requested_num; i++) {
        float t = filter->rs_phase;
        samples[i][0] = filter->rs_prev[0] + (filter->rs_cur[0] - filter->rs_prev[0]) * t;
        samples[i][1] = filter->rs_prev[1] + (filter->rs_cur[1] - filter->rs_prev[1]) * t;
        filter->rs_phase += step;
        while (filter->rs_phase >= 1.0f) {
            filter->rs_prev[0] = filter->rs_cur[0];
            filter->rs_prev[1] = filter->rs_cur[1];
            if (!rs_next_frame(d, v, filter, filter->rs_cur)) {
                /* Out of source: hand back what was produced. */
                return i > 0 ? i : -1;
            }
            filter->rs_phase -= 1.0f;
        }
    }
    return requested_num;
}

/* ============================================================
 * Voice processing (main per-voice function)
 * ============================================================ */

static void voice_process(MCPXAPUState *d,
                          float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                          float sample_buf[NUM_SAMPLES_PER_FRAME][2],
                          uint16_t v, int voice_list)
{
    if (!RECOMP_CHECK(v < MCPX_HW_MAX_VOICES, "voice %u", v))
        return;
    bool stereo = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT,
                                 NV_PAVS_VOICE_CFG_FMT_STEREO) != 0;
    unsigned int channels = stereo ? 2 : 1;
    bool paused = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                 NV_PAVS_VOICE_PAR_STATE_PAUSED) != 0;

    struct McpxApuDebugVoice *dbg = &g_dbg.vp.v[v];
    dbg->active = true;
    dbg->stereo = stereo;
    dbg->paused = paused;

    if (paused) return;

    /* Step filter envelope */
    float ef_value = voice_step_envelope(
        d, v, NV_PAVS_VOICE_CFG_ENV1, NV_PAVS_VOICE_CFG_ENVF,
        NV_PAVS_VOICE_CFG_MISC, NV_PAVS_VOICE_CFG_MISC_EF_RELEASERATE,
        NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EFLVL,
        NV_PAVS_VOICE_CUR_ECNT_EFCOUNT, NV_PAVS_VOICE_PAR_STATE_EFCUR);
    if (ef_value < 0.0f) ef_value = 0.0f;
    if (ef_value > 1.0f) ef_value = 1.0f;

    int16_t p = (int16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_PITCH_LINK,
                                         NV_PAVS_VOICE_TAR_PITCH_LINK_PITCH);
    int8_t ps = (int8_t)voice_get_mask(d, v, NV_PAVS_VOICE_CFG_ENV0,
                                        NV_PAVS_VOICE_CFG_ENV0_EF_PITCHSCALE);
    float rate = 1.0f / powf(2.0f, (p + ps * 32 * ef_value) / 4096.0f);
    dbg->rate = rate;

    /* Step amplitude envelope */
    float ea_value = voice_step_envelope(
        d, v, NV_PAVS_VOICE_CFG_ENV0, NV_PAVS_VOICE_CFG_ENVA,
        NV_PAVS_VOICE_TAR_LFO_ENV, NV_PAVS_VOICE_TAR_LFO_ENV_EA_RELEASERATE,
        NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_EALVL,
        NV_PAVS_VOICE_CUR_ECNT_EACOUNT, NV_PAVS_VOICE_PAR_STATE_EACUR);
    if (ea_value < 0.0f) ea_value = 0.0f;
    if (ea_value > 1.0f) ea_value = 1.0f;

    float samples[NUM_SAMPLES_PER_FRAME][2];
    memset(samples, 0, sizeof(samples));

    bool multipass = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT,
                                    NV_PAVS_VOICE_CFG_FMT_MULTIPASS) != 0;
    dbg->multipass = multipass;

    if (multipass) {
        /* Read from multipass bin */
        int mp_bin = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT,
                                    NV_PAVS_VOICE_CFG_FMT_MULTIPASS_BIN);
        dbg->multipass_bin = (uint8_t)mp_bin;
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            samples[i][0] = mixbins[mp_bin][i];
            samples[i][1] = mixbins[mp_bin][i];
        }
        bool clear_mix = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT,
                                        NV_PAVS_VOICE_CFG_FMT_CLEAR_MIX) != 0;
        if (clear_mix) {
            memset(&mixbins[mp_bin][0], 0, sizeof(mixbins[0]));
        }
    } else {
        for (int sample_count = 0; sample_count < NUM_SAMPLES_PER_FRAME;) {
            int active = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                        NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
            if (!active) return;
            int count = voice_resample(d, v, &samples[sample_count],
                                       NUM_SAMPLES_PER_FRAME - sample_count, rate);
            if (count < 0) break;
            sample_count += count;
        }
    }

    int active = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
    if (!active) return;

    /* Get volume bins */
    int bin[8];
    bin[0] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V0BIN);
    bin[1] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V1BIN);
    bin[2] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V2BIN);
    bin[3] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V3BIN);
    bin[4] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V4BIN);
    bin[5] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V5BIN);
    bin[6] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_V6BIN);
    bin[7] = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_V7BIN);

    if (v < MCPX_HW_MAX_3D_VOICES) {
        bin[0] = d->vp.hrtf_submix[0];
        bin[1] = d->vp.hrtf_submix[1];
        bin[2] = d->vp.hrtf_submix[2];
        bin[3] = d->vp.hrtf_submix[3];
    }

    uint16_t vol[8];
    vol[0] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLA, NV_PAVS_VOICE_TAR_VOLA_VOLUME0);
    vol[1] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLA, NV_PAVS_VOICE_TAR_VOLA_VOLUME1);
    vol[2] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLB, NV_PAVS_VOICE_TAR_VOLB_VOLUME2);
    vol[3] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLB, NV_PAVS_VOICE_TAR_VOLB_VOLUME3);
    vol[4] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLC, NV_PAVS_VOICE_TAR_VOLC_VOLUME4);
    vol[5] = (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLC, NV_PAVS_VOICE_TAR_VOLC_VOLUME5);
    vol[6] = (uint16_t)(voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLC, NV_PAVS_VOICE_TAR_VOLC_VOLUME6_B11_8) << 8);
    vol[6] |= (uint16_t)(voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLB, NV_PAVS_VOICE_TAR_VOLB_VOLUME6_B7_4) << 4);
    vol[6] |= (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLA, NV_PAVS_VOICE_TAR_VOLA_VOLUME6_B3_0);
    vol[7] = (uint16_t)(voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLC, NV_PAVS_VOICE_TAR_VOLC_VOLUME7_B11_8) << 8);
    vol[7] |= (uint16_t)(voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLB, NV_PAVS_VOICE_TAR_VOLB_VOLUME7_B7_4) << 4);
    vol[7] |= (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_TAR_VOLA, NV_PAVS_VOICE_TAR_VOLA_VOLUME7_B3_0);

    for (int i = 0; i < 8; i++) {
        dbg->bin[i] = (uint8_t)bin[i];
        dbg->vol[i] = vol[i];
    }

    if (voice_should_mute(v)) return;

    /* Low-pass filter */
    int fmode = voice_get_mask(d, v, NV_PAVS_VOICE_CFG_MISC,
                               NV_PAVS_VOICE_CFG_MISC_FMODE);
    bool lpf = false;
    if (v < MCPX_HW_MAX_3D_VOICES) {
        lpf = (fmode == 1);
    } else {
        lpf = stereo ? (fmode == 1) : (fmode & 1) != 0;
    }
    if (lpf) {
        for (int ch = 0; ch < 2; ch++) {
            int16_t fc = (int16_t)voice_get_mask(
                d, v, NV_PAVS_VOICE_TAR_FCA + (ch % channels) * 4,
                NV_PAVS_VOICE_TAR_FCA_FC0);
            float fc_f = clampf(powf(2, fc / 4096.0f), 0.003906f, 1.0f);
            uint16_t q = (uint16_t)voice_get_mask(
                d, v, NV_PAVS_VOICE_TAR_FCA + (ch % channels) * 4,
                NV_PAVS_VOICE_TAR_FCA_FC1);
            float q_f = clampf(q / (1.0f * 0x8000), 0.079407f, 1.0f);
            sv_filter *filter = &d->vp.filters[v].svf[ch];
            setup_svf(filter, fc_f, q_f, F_LP);
            for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                samples[i][ch] = run_svf(filter, samples[i][ch]);
                samples[i][ch] = fminf(fmaxf(samples[i][ch], -1.0f), 1.0f);
            }
        }
    }

    /* HRTF processing for 3D voices.
     *
     * Audio > Audio effects (RECOMP_APU_GPFX) places them from their
     * settings instead, with no filter (apu_hrtf.h), into ears[] for the
     * HRTF submixes, and leaves samples[] dry for the voice's other bins. */
    float ears[NUM_SAMPLES_PER_FRAME][2];
    int placed = 0;
    if (v < MCPX_HW_MAX_3D_VOICES && (g_config.audio.hrtf || apu_gpfx_on())) {
        uint16_t hrtf_handle =
            (uint16_t)voice_get_mask(d, v, NV_PAVS_VOICE_CFG_HRTF_TARGET,
                                     NV_PAVS_VOICE_CFG_HRTF_TARGET_HANDLE);
        if (hrtf_handle != HRTF_NULL_HANDLE && g_config.audio.hrtf) {
            hrtf_filter_process(&d->vp.filters[v].hrtf, samples, samples);
        } else if (!g_config.audio.hrtf) {
            int64_t t0 = apu_gpfx_timed() ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;
            ApuGpfxPlaced where;
            placed = hrtf_handle < HRTF_ENTRY_COUNT;
            if (placed) {
                memcpy(ears, samples, sizeof ears);
                apu_hrtf_place(&d->vp.filters[v].hrtf, &s_ears[v],
                               d->vp.hrtf.entries[hrtf_handle].hrir,
                               d->vp.hrtf.entries[hrtf_handle].itd, ears, &where);
            }
            apu_gpfx_voice(v, bin, placed ? &where : NULL,
                           t0 && placed ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - t0 : 0);
        }
    }

    /*
     * RECOMP_APU_VDUMP=<voice>:<path>: the samples one voice actually
     * decodes, in the order it decodes them, as 48 kHz stereo s16.
     *
     * The buffer a streaming voice reads is refilled by the title as the
     * voice consumes it, so "the bytes in the buffer are correct" and "the
     * voice plays the music" are different claims: a read position that runs
     * at the wrong speed, or a refill that lands on samples not yet played,
     * scrambles correct bytes into a wash. The buffer dump answers the first
     * question and this answers the second -- and it can be laid directly
     * against a reference decode of the title's own .adx.
     */
    { static int init; static FILE *vf; static int want = -1;
      if (!init) { const char *e = getenv("RECOMP_APU_VDUMP");
          init = 1;
          if (e) { const char *c = strchr(e, ':');
                   if (c) { want = atoi(e); vf = fopen(c + 1, "wb");
                            if (vf) fprintf(stderr, "  [APU] dumping voice %d's "
                                    "decoded stream to %s\n", want, c + 1); } } }
      if (vf && v == (uint16_t)want) {
          int16_t out[NUM_SAMPLES_PER_FRAME][2]; int i;
          for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
              float l = samples[i][0], r = samples[i][1];
              l = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
              r = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
              out[i][0] = (int16_t)(l * 32767.0f);
              out[i][1] = (int16_t)(r * 32767.0f);
          }
          fwrite(out, sizeof out, 1, vf);
          fflush(vf);
      } }

    /*
     * RECOMP_APU_CBO=<voice>: the read position, ten times a second.
     *
     * The other half of the same question: the title refills the ring from
     * one side while the voice reads from the other, and the only way they
     * can stay apart is if the position the voice reports advances at the
     * rate the title expects. Sampled often enough to see a jump, not so
     * often as to fill the log.
     */
    { static int want = -2; static uint64_t last_ms; static uint32_t prev; static int n;
      if (want == -2) { const char *e = getenv("RECOMP_APU_CBO");
                        want = e ? atoi(e) : -1; }
      if (want >= 0 && v == (uint16_t)want) {
          uint64_t now = (uint64_t)qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
          if (now - last_ms >= 100 && n++ < 200) {
              uint32_t cbo_now = voice_get_mask(d, v, NV_PAVS_VOICE_PAR_OFFSET,
                                                NV_PAVS_VOICE_PAR_OFFSET_CBO);
              fprintf(stderr, "  [CBO] voice %u  cbo %6u  (+%6d since %llu ms)\n",
                      v, cbo_now, (int)(cbo_now - prev),
                      (unsigned long long)(last_ms ? now - last_ms : 0));
              prev = cbo_now; last_ms = now; fflush(stderr);
          }
      } }

    /* RECOMP_APU_VOICE=1: where a voice's sound is lost.
     *
     * An active voice that produces silence has three quite different causes
     * -- it decoded nothing, it decoded something and the envelope is at zero,
     * or it decoded something at full envelope and every mixbin gain is zero
     * -- and from the encode processor's output they are indistinguishable.
     * Printed once a second per voice, so a running title stays readable. */
    {
        static int trace = -1;
        static int64_t last_ms[MCPX_HW_MAX_VOICES];
        if (trace < 0) trace = getenv("RECOMP_APU_VOICE") ? 1 : 0;
        if (trace && v < MCPX_HW_MAX_VOICES) {
            int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
            if (now - last_ms[v] >= 1000) {
                float pk = 0.0f;
                int i, b;
                for (i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                    float a = samples[i][0] < 0 ? -samples[i][0] : samples[i][0];
                    if (a > pk) pk = a;
                }
                fprintf(stderr, "  [VOICE %3u] decoded peak %.4f  envelope %.4f"
                        "  rate %.3f (~%.0f Hz source)"
                        "  bins/vols", v, (double)pk, (double)ea_value,
                        (double)rate, (double)(rate > 1e-4f ? 48000.0f / rate : 0.0f));
                for (b = 0; b < 8; b++)
                    fprintf(stderr, " %u:%.3f", bin[b],
                            (double)attenuate(vol[b]));
                fprintf(stderr, "\n");
                fflush(stderr);
                last_ms[v] = now;
            }
        }
    }

    /* Mix into bins */
    float gain[8];
    for (int b = 0; b < 8; b++) {
        float g = ea_value;
        float hr;
        if ((v < MCPX_HW_MAX_3D_VOICES) && (b < 4)) {
            hr = (float)(1 << d->vp.hrtf_headroom);
        } else {
            hr = (float)(1 << d->vp.submix_headroom[bin[b]]);
        }
        g *= attenuate(vol[b]) / hr;
        gain[b] = g;
        if (placed && b < 4) {
            /* The HRTF submixes take the ears two at a time, the left's
             * then the right's: DirectSound lists them front left, back
             * left, front right, back right. */
            for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++)
                mixbins[bin[b]][i] += g * ears[i][b >> 1];
            continue;
        }
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            mixbins[bin[b]][i] += g * samples[i][b % channels];
        }
    }
    /* RECOMP_APU_CHANNELS: what this voice sends to each side (apu_channels.c). */
    if (g_apu_ch_capture)
        apu_ch_voice(v, channels, samples, bin, gain, lpf);

    /* VP monitor mix */
    if (d->monitor.point == MCPX_APU_DEBUG_MON_VP) {
        float g = 0.0f;
        for (int b = 0; b < 8; b++) {
            float hr = (float)(1 << d->vp.submix_headroom[bin[b]]);
            float bg = attenuate(vol[b]) / hr;
            if (bg > g) g = bg;
        }
        g *= ea_value;
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            sample_buf[i][0] += g * samples[i][0];
            sample_buf[i][1] += g * samples[i][1];
        }
    }

    (void)voice_list;
}

/* ============================================================
 * VP Frame - Process all voice lists
 *
 * Simplified single-threaded version (no worker threads initially)
 * ============================================================ */

void mcpx_apu_vp_frame(MCPXAPUState *d,
                        float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    /* The active voices of the three lists, mixed once none is locked */
    struct { uint16_t v; int list; } queue[3 * MCPX_HW_MAX_VOICES];
    int queue_len = 0;
    int64_t wait_start = 0, wait_us = 0;

    memset(d->vp.sample_buf, 0, sizeof(d->vp.sample_buf));
    __atomic_store_n(&s_vp_frame, s_vp_frame + 1, __ATOMIC_RELAXED);

    for (int list = 0; list < 3; list++) {
        hwaddr top, current, next;
        top = voice_list_regs[list].top;
        current = voice_list_regs[list].current;
        next = voice_list_regs[list].next;

        d->regs[current] = d->regs[top];

        for (int i = 0; d->regs[current] != 0xFFFF; i++) {
            if (i >= MCPX_HW_MAX_VOICES) {
                DPRINTF("Voice list contains invalid entry!\n");
                break;
            }

            uint16_t v = (uint16_t)d->regs[current];
            d->regs[next] = voice_get_mask(d, v, NV_PAVS_VOICE_TAR_PITCH_LINK,
                               NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE);

            if (!voice_get_mask(d, v, NV_PAVS_VOICE_PAR_STATE,
                                NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE)) {
                /* Only while the front end is free to take it. The voice
                 * processor now keeps running while the front end is trapped
                 * (apu_core.c), and a second report then would overwrite the
                 * method and parameter the title's handler is about to read
                 * (FEDECMETH/FEDECPARAM). An idle voice stays in its list
                 * until the handler takes it out, so it is simply reported
                 * again on the first frame after the trap clears -- the
                 * hardware queues these; this re-detects them. */
                int report = (d->regs[NV_PAPU_FECTL] & NV_PAPU_FECTL_FEMETHMODE)
                             == NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING;
                if (report)
                    fe_method(d, SE2FE_IDLE_VOICE, v);
                g1_idle(v, report);
            } else {
                if (v < MCPX_HW_MAX_VOICES) s_active_seen[v] = s_vp_frame;
                queue[queue_len].v = v;
                queue[queue_len].list = list;
                queue_len++;
            }
            d->regs[current] = d->regs[next];
        }
    }
    g1_walked();

    /* Mix a voice only once the title has finished changing it, as xemu
     * does (voice_work_dispatch).
     *
     * The title brackets a batch of changes to a voice with VOICE_LOCK, and
     * makes them from its own thread while this one runs. A voice mixed in
     * between plays half-changed, and what the frame writes back to it (the
     * play position, the envelope) can overwrite what the title has just
     * set. So the idle voices are reported first, as before, and the frame
     * waits until none of the active ones is locked, letting go of d->lock
     * meanwhile. voice_lock and mcpx_apu_wait_for_idle each signal d->cond
     * under d->lock, so the wait misses neither an unlock nor a pause, and
     * the frame is given up if the APU is being paused. */
    for (;;) {
        int i;
        for (i = 0; i < queue_len; i++)
            if (is_voice_locked(d, queue[i].v)) break;
        if (i == queue_len) break;
        if (d->pause_requested) {
            queue_len = 0;
            break;
        }
        if (!wait_start) wait_start = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
        qemu_cond_wait(&d->cond, &d->lock);
    }
    if (wait_start) wait_us = qemu_clock_get_us(QEMU_CLOCK_REALTIME) - wait_start;

    for (int i = 0; i < queue_len; i++) {
        watch_before(d, queue[i].v);
        voice_process(d, mixbins, d->vp.sample_buf, queue[i].v, queue[i].list);
        watch_after(d, queue[i].v);
    }
    g1_report();

    /* RECOMP_APU_STATS: how many frames waited for a voice the title had
     * locked, and for how long. */
    {
        static int stats = -1;
        static int64_t last_ms, total_us, longest_us;
        static int waits;
        if (stats < 0) stats = getenv("RECOMP_APU_STATS") ? 1 : 0;
        if (stats) {
            int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
            if (wait_start) {
                waits++;
                total_us += wait_us;
                if (wait_us > longest_us) longest_us = wait_us;
            }
            if (!last_ms) last_ms = now;
            if (now - last_ms >= 1000) {
                fprintf(stderr, "[APU] voice locks: %d frames waited, %.1f ms "
                        "in all (longest %.1f ms)\n", waits, total_us / 1000.0,
                        longest_us / 1000.0);
                fflush(stderr);
                last_ms = now; waits = 0; total_us = longest_us = 0;
            }
        }
    }

    /* VP monitor output (RECOMP_APU_MIX=xemu, mcpx_apu_vp_init).
     *
     * Written, not added as in xemu: nothing here clears the frame between
     * one pass of the eight sub-frames and the next (the fold in apu_dsp.c
     * writes over it), so adding piled each frame onto the one before. The
     * mixbins are left as they are, for RECOMP_APU_BINS and
     * RECOMP_APU_CHANNELS to report; nothing else reads them after this. */
    if (d->monitor.point == MCPX_APU_DEBUG_MON_VP) {
        int16_t isamp[NUM_SAMPLES_PER_FRAME * 2];
        src_float_to_short_array((float *)d->vp.sample_buf, isamp,
                                 NUM_SAMPLES_PER_FRAME * 2);
        int off = (d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME;
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            d->monitor.frame_buf[off + i][0] = isamp[2 * i];
            d->monitor.frame_buf[off + i][1] = isamp[2 * i + 1];
        }
        memset(d->vp.sample_buf, 0, sizeof(d->vp.sample_buf));
    }
}

/* ============================================================
 * VP Init / Finalize / Reset
 * ============================================================ */

void mcpx_apu_vp_init(MCPXAPUState *d)
{
    /* Single-threaded - no worker dispatch needed */

    /* The trap's and the watch's switches, read once, before the threads. */
    s_vp_stats = getenv("RECOMP_APU_STATS") ? 1 : 0;
    s_vp_watch = getenv("RECOMP_APU_WATCH") ? 1 : 0;

    /* RECOMP_APU_MIX=xemu: the sound as xemu plays it with Use DSP off, as
     * it comes set. Each voice's own left and right at the loudest of its
     * eight volumes, and the 3D voices through the HRTF filter, whichever
     * mixbins the title sends them to -- in place of the mixbins folded to
     * stereo (apu_dsp.c). A sound that loses a side with the fold and keeps
     * it with this lost it in the routing or the fold. */
    const char *mix = getenv("RECOMP_APU_MIX");
    if (mix && !strcmp(mix, "xemu")) {
        d->monitor.point = MCPX_APU_DEBUG_MON_VP;
        g_config.audio.hrtf = true;
        fprintf(stderr, "[APU] RECOMP_APU_MIX=xemu: each voice's own left and "
                        "right, the 3D voices through HRTF, as xemu plays them\n");
    }
}

void mcpx_apu_vp_finalize(MCPXAPUState *d)
{
    (void)d;
}

void mcpx_apu_vp_reset(MCPXAPUState *d)
{
    d->vp.ssl_base_page = 0;
    d->vp.hrtf_headroom = 0;
    memset(d->vp.ssl, 0, sizeof(d->vp.ssl));
    memset(d->vp.hrtf_submix, 0, sizeof(d->vp.hrtf_submix));
    memset(d->vp.submix_headroom, 0, sizeof(d->vp.submix_headroom));
    memset(d->vp.voice_locked, 0, sizeof(d->vp.voice_locked));
    for (int v = 0; v < MCPX_HW_MAX_VOICES; v++) {
        hrtf_filter_init(&d->vp.filters[v].hrtf);
    }
    memset(s_ears, 0, sizeof(s_ears));
}
