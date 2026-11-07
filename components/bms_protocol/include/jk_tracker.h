#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Byte-by-byte JK frame tracker for raw views: finds headers, follows the
 * position inside a frame and checks the checksum. It does not decode; its job
 * is to tell which bytes must be masked before anything is printed or shown.
 */
typedef struct {
    uint32_t shift;     /* last four bytes */
    int pos;            /* position in the current frame, -1 when outside one */
    uint8_t type, counter, sum;
} jk_tracker_t;

typedef enum {
    JK_TRACK_NONE,
    JK_TRACK_HEADER,        /* this byte completed a header */
    JK_TRACK_FRAME_OK,      /* this byte ended a frame with a valid checksum */
    JK_TRACK_FRAME_BAD,     /* ... with a wrong checksum */
} jk_track_event_t;

void jk_tracker_reset(jk_tracker_t *t);

/** Feed one byte. *masked is set if the byte is a BLE PIN or password byte. */
jk_track_event_t jk_tracker_push(jk_tracker_t *t, uint8_t b, bool *masked);
