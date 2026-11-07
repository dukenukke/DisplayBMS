#include "jk_tracker.h"

#include "jk_frame.h"

void jk_tracker_reset(jk_tracker_t *t)
{
    *t = (jk_tracker_t){.pos = -1};
}

jk_track_event_t jk_tracker_push(jk_tracker_t *t, uint8_t b, bool *masked)
{
    jk_track_event_t ev = JK_TRACK_NONE;
    *masked = false;
    if (t->pos >= 0) {
        ++t->pos;
        if (t->pos == JK_FRAME_TYPE_POS) t->type = b;
        if (t->pos == JK_FRAME_COUNTER_POS) t->counter = b;
        *masked = jk_frame_byte_sensitive(t->type, t->pos);
        if (t->pos < JK_FRAME_SUM_POS) {
            t->sum += b;
        } else {
            ev = t->sum == b ? JK_TRACK_FRAME_OK : JK_TRACK_FRAME_BAD;
            t->pos = -1;
        }
    }
    /* A header restarts tracking even inside a frame (truncated frame, resync). */
    t->shift = t->shift << 8 | b;
    if (t->shift == JK_FRAME_HEADER) {
        t->pos = JK_FRAME_TYPE_POS - 1;
        t->sum = (uint8_t)(0x55 + 0xAA + 0xEB + 0x90);
        t->type = 0;
        ev = JK_TRACK_HEADER;
    }
    return ev;
}
