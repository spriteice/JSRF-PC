/* Run with: python tests/psh_consts/run.py
 *
 * src/nv2a_gl/nv2a_gl.c whole, with no surface, so the renderer never starts
 * and no GL call is made: the combiner constants as uniforms
 * (nv2a_psh_consts.h), from the front half's side.
 * - The program key (psh_hash) stays the same while the title writes new
 *   constants and a new alpha reference, and changes with a combiner word.
 * - The shader cache is handed one state for both (sc_state_note), the
 *   combiner part of it the key.
 * - The constants go to the program (psh_consts_send) when they changed
 *   since it last had them, and after a program switch, as pshC[] holds
 *   them; not when nothing changed, nor to a program that doesn't read them.
 * The shader cache and the device are stand-ins here, which keep what they
 * are handed.
 */
#include "nv2a_gl.c"

#define STATE_MAX (NV2A_VSH_MAX_INSNS * 16 + sizeof(Nv2aPshState))

static unsigned char s_state[2][STATE_MAX];
static uint32_t s_state_len[2];
static int s_states;

int nv2a_sc_is_open(void) { return 1; }

void nv2a_sc_state_add(uint32_t layout, uint32_t kind, const void *data, uint32_t len)
{
    (void)layout; (void)kind;
    if (s_states < 2 && len <= STATE_MAX) {
        memcpy(s_state[s_states], data, len);
        s_state_len[s_states] = len;
    }
    s_states++;
}

static int s_reads = 1, s_sent, s_sent_n;
static float s_sent_v[NV2A_PSH_NCONSTS * 4];

static int dev_prog_uses(uint32_t prog, int id) { (void)prog; return s_reads && id == NV2A_U_PSHC; }

static void dev_uniform_f(int id, const float *v, int n)
{
    if (id != NV2A_U_PSHC) return;
    s_sent++;
    s_sent_n = n;
    memcpy(s_sent_v, v, sizeof s_sent_v);
}

static Nv2aGfx s_dev;
static int s_fails;

static void expect(int ok, const char *what)
{
    if (!ok) { printf("FAIL %s\n", what); s_fails++; }
}

/* Whether the constants last sent are pshC[] as these words make it. */
static int sent_as(void)
{
    uint32_t w[NV2A_PSH_NCONSTS];
    float v[NV2A_PSH_NCONSTS * 4];
    nv2a_psh_const_words(w, &S.psh);
    nv2a_psh_const_values(v, w);
    return s_sent_n == NV2A_PSH_NCONSTS && !memcmp(v, s_sent_v, sizeof v);
}

int main(void)
{
    static const unsigned char head[16] = { 1, 2, 3, 4 };   /* a vertex program's words */
    uint32_t h;
    int i, sent;

    g_on = 1;
    g_prof_on = 0;

    /* Two stages and a final combiner, as a title sets them up. */
    nv2a_gl_method(M_SET_COMBINER_CONTROL, 2u | (1u << 12) | (1u << 16));
    nv2a_gl_method(M_SET_COMBINER_COLOR_ICW, 0x01200000u);
    nv2a_gl_method(M_SET_COMBINER_COLOR_OCW, 0x0C0u);
    nv2a_gl_method(M_SET_COMBINER_COLOR_ICW + 4, 0x0C200220u);
    nv2a_gl_method(M_SET_COMBINER_COLOR_OCW + 4, 0xC00u);
    nv2a_gl_method(M_SET_COMBINER_SPECFOG_CW0, 0x00000C0Fu);
    nv2a_gl_method(M_SET_COMBINER_SPECFOG_CW1, 0x01201200u);
    for (i = 0; i < NV2A_PSH_STAGES; i++) {
        nv2a_gl_method(M_SET_COMBINER_FACTOR0 + 4 * i, 0x10203040u + (uint32_t)i);
        nv2a_gl_method(M_SET_COMBINER_FACTOR1 + 4 * i, 0x50607080u + (uint32_t)i);
    }
    nv2a_gl_method(0x1E20, 0x11223344u);   /* NV097_SET_SPECULAR_FOG_FACTOR */
    nv2a_gl_method(0x1E24, 0x55667788u);
    S.psh.alpha_ref = 0.25f;

    /* The key, through a fade: each constant and the reference changed. */
    h = psh_hash();
    sc_state_note(SC_PROGRAM, head, sizeof head);
    for (i = 0; i < NV2A_PSH_STAGES; i++) {
        nv2a_gl_method(M_SET_COMBINER_FACTOR0 + 4 * i, 0x01010101u * (uint32_t)i);
        nv2a_gl_method(M_SET_COMBINER_FACTOR1 + 4 * i, 0xFF000000u - (uint32_t)i);
        expect(psh_hash() == h, "the key, with a stage's constants changed");
    }
    nv2a_gl_method(0x1E20, 0xA0B0C0D0u);
    expect(psh_hash() == h, "the key, with the final combiner's C0 changed");
    nv2a_gl_method(0x1E24, 0x0A0B0C0Du);
    expect(psh_hash() == h, "the key, with the final combiner's C1 changed");
    S.psh.alpha_ref = 0.75f;
    expect(psh_hash() == h, "the key, with the alpha reference changed");
    sc_state_note(SC_PROGRAM, head, sizeof head);

    /* The shader cache: one state for both, its combiner part the key. */
    expect(s_states == 2 && s_state_len[0] == sizeof head + sizeof(Nv2aPshState)
           && s_state_len[1] == s_state_len[0], "two states handed over, each the whole state");
    expect(!memcmp(s_state[0], s_state[1], s_state_len[0]), "the same state for both");
    { Nv2aPshState key;
      nv2a_psh_key(&key, &S.psh);
      expect(!memcmp(s_state[1] + sizeof head, &key, sizeof key), "the combiner part is the key"); }

    /* ... and a different program when a combiner changes. */
    nv2a_gl_method(M_SET_COMBINER_COLOR_ICW + 4, 0x0C200120u);
    expect(psh_hash() != h, "the key, with a combiner word changed");
    nv2a_gl_method(M_SET_COMBINER_COLOR_ICW + 4, 0x0C200220u);
    expect(psh_hash() == h, "the key, with the combiner word back");

    /* The constants sent: the first time, not again until one changes, again
     * after a program switch, and never to a program that doesn't read them. */
    s_dev.prog_uses = dev_prog_uses;
    s_dev.uniform_f = dev_uniform_f;
    G = &s_dev;
    g_pcache[0].prog = 1;
    psh_consts_send(0);
    expect(s_sent == 1 && sent_as(), "sent the first time, as pshC[] holds them");
    psh_consts_send(0);
    expect(s_sent == 1, "not sent again unchanged");
    nv2a_gl_method(M_SET_COMBINER_FACTOR1 + 4 * 2, 0x00FF8000u);
    psh_consts_send(0);
    expect(s_sent == 2 && sent_as(), "sent when a stage's C1 changed");
    expect(s_sent_v[4 * NV2A_PSH_C1(2) + 1] > 0.5f && s_sent_v[4 * NV2A_PSH_C1(2) + 1] < 0.51f,
           "stage 2's C1 at its place, its green half way");
    nv2a_gl_method(0x1E24, 0x40000000u);
    psh_consts_send(0);
    expect(s_sent == 3 && sent_as(), "sent when the final combiner's C1 changed");
    g_last.psh_ok = 0;                      /* as a program switch leaves it */
    psh_consts_send(0);
    expect(s_sent == 4 && sent_as(), "sent after a program switch");
    sent = s_sent;
    s_reads = 0;
    nv2a_gl_method(M_SET_COMBINER_FACTOR0, 0x12345678u);
    psh_consts_send(0);
    expect(s_sent == sent, "not sent to a program that doesn't read them");

    if (s_fails) { printf("psh_consts: %d case(s) FAIL\n", s_fails); return 1; }
    printf("psh_consts: the key, the shader cache's state and the constants sent all pass\n");
    return 0;
}
