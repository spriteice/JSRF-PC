/* rumble_hold.c, driven as xbox_InputSetState and each pad read drive it. */
#include <stdio.h>
#include <string.h>
#include "rumble_hold.h"

static int g_bad;

static void expect(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL: %s: %s, should be %s\n", what, got ? "sent" : "not sent",
               want ? "sent" : "not sent");
        g_bad = 1;
    }
}

int main(void)
{
    RumbleHold h;
    double t;
    memset(&h, 0, sizeof h);

    /* Nothing set: nothing to send, to any pad. */
    expect("nothing set, a pad read", rumble_hold_due(&h, 0, 0.0), 0);

    /* The game sets a vibration once (JSRF calls only on a change). */
    rumble_hold_set(&h, 40000, 0);
    expect("set", rumble_hold_due(&h, 0, 1.0), 1);
    expect("the next read", rumble_hold_due(&h, 0, 1.02), 0);

    /* It is held: sent again before SDL's time for it runs out, at every
     * read of the pad from then on, whatever the time between sends. The
     * old code sent it for one second, once, and it stopped. */
    for (t = 1.0; t < 600.0; t += 0.0167) {
        rumble_hold_due(&h, 0, t);
        if (t - h.sent >= RUMBLE_HOLD_MS / 1000.0) {
            printf("FAIL: held %.1f s without a send; SDL stops it after %.1f s\n",
                   t - h.sent, RUMBLE_HOLD_MS / 1000.0);
            g_bad = 1;
            break;
        }
    }
    if (!(RUMBLE_RENEW_S < RUMBLE_HOLD_MS / 1000.0)) {
        printf("FAIL: renewed every %.1f s, but SDL holds it %.1f s\n",
               RUMBLE_RENEW_S, RUMBLE_HOLD_MS / 1000.0);
        g_bad = 1;
    }

    /* The same speeds again change nothing; others go at once. */
    memset(&h, 0, sizeof h);
    rumble_hold_set(&h, 40000, 0);
    expect("set again", rumble_hold_due(&h, 0, 0.0), 1);
    rumble_hold_set(&h, 40000, 0);
    expect("the same speeds", rumble_hold_due(&h, 0, 0.1), 0);
    rumble_hold_set(&h, 40000, 20000);
    expect("the right motor too", rumble_hold_due(&h, 0, 0.2), 1);

    /* A pad found later, or found again, gets what is running. */
    expect("another pad", rumble_hold_due(&h, 7, 0.3), 1);
    expect("that pad's next read", rumble_hold_due(&h, 7, 0.4), 0);

    /* Stopped: sent once, then nothing to renew. */
    rumble_hold_set(&h, 0, 0);
    expect("stopped", rumble_hold_due(&h, 7, 0.5), 1);
    expect("stopped, long after", rumble_hold_due(&h, 7, 500.0), 0);
    expect("stopped, a pad found later", rumble_hold_due(&h, 9, 501.0), 0);

    if (g_bad) return 1;
    printf("rumble hold: all cases pass\n");
    return 0;
}
