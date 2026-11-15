/* A pad's motors, held as the console holds them (rumble_hold.h). */
#include "rumble_hold.h"

void rumble_hold_set(RumbleHold *h, unsigned short left, unsigned short right)
{
    if (h->left != left || h->right != right) h->changed = 1;
    h->left = left;
    h->right = right;
}

int rumble_hold_due(RumbleHold *h, long pad, double now)
{
    int running = h->left || h->right, due;
    if (!h->has_pad || h->pad != pad)
        due = running || h->changed;            /* a pad that hasn't had them */
    else
        due = h->changed || (running && now - h->sent >= RUMBLE_RENEW_S);
    if (!due) return 0;
    h->pad = pad;
    h->has_pad = 1;
    h->sent = now;
    h->changed = 0;
    return 1;
}
