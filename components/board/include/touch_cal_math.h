/*
 * Four-point affine touch calibration math.
 * Adapted from the fungen project (same author, GPL-3.0).
 * Header-only and free of ESP-IDF dependencies so it can be unit-tested on the host.
 */
#pragma once

#include <math.h>
#include <stdbool.h>

typedef struct {
    float x, y;
} touch_cal_point_t;

/* screen = M * raw + offset */
typedef struct {
    float xx, xy, x_offset;
    float yx, yy, y_offset;
} touch_cal_mapping_t;

/* Fit rejection limits, in raw ADC units and screen pixels. */
#define TOUCH_CAL_MIN_SPREAD        10000.0f
#define TOUCH_CAL_MIN_DET_RATIO     0.01f
#define TOUCH_CAL_MAX_RESIDUAL_PX   12.0f

static inline touch_cal_point_t touch_cal_map(const touch_cal_mapping_t *m, touch_cal_point_t p)
{
    return (touch_cal_point_t) {
        m->xx * p.x + m->xy * p.y + m->x_offset,
        m->yx * p.x + m->yy * p.y + m->y_offset,
    };
}

static inline bool touch_cal_mapping_valid(const touch_cal_mapping_t *m)
{
    return isfinite(m->xx) && isfinite(m->xy) && isfinite(m->x_offset) &&
           isfinite(m->yx) && isfinite(m->yy) && isfinite(m->y_offset) &&
           fabsf(m->xx * m->yy - m->xy * m->yx) > 1e-6f;
}

/*
 * Least-squares affine fit over four points (top-left, top-right, bottom-left,
 * bottom-right). Handles any axis swap or mirroring of the sensor.
 */
static inline bool touch_cal_fit(const touch_cal_point_t raw[4], const touch_cal_point_t target[4],
                                 touch_cal_mapping_t *m)
{
    touch_cal_point_t r = {0}, t = {0};
    for (int i = 0; i < 4; ++i) {
        if (!isfinite(raw[i].x) || !isfinite(raw[i].y) ||
            !isfinite(target[i].x) || !isfinite(target[i].y)) return false;
        r.x += raw[i].x / 4; r.y += raw[i].y / 4;
        t.x += target[i].x / 4; t.y += target[i].y / 4;
    }
    float xx = 0, xy = 0, yy = 0, ux = 0, uy = 0, vx = 0, vy = 0;
    for (int i = 0; i < 4; ++i) {
        float x = raw[i].x - r.x, y = raw[i].y - r.y;
        float u = target[i].x - t.x, v = target[i].y - t.y;
        xx += x * x; xy += x * y; yy += y * y;
        ux += u * x; uy += u * y; vx += v * x; vy += v * y;
    }
    float det = xx * yy - xy * xy;
    /* Reject repeated points and nearly collinear input rather than divide by zero. */
    if (xx < TOUCH_CAL_MIN_SPREAD || yy < TOUCH_CAL_MIN_SPREAD ||
        det <= xx * yy * TOUCH_CAL_MIN_DET_RATIO) return false;
    m->xx = (ux * yy - uy * xy) / det;
    m->xy = (uy * xx - ux * xy) / det;
    m->yx = (vx * yy - vy * xy) / det;
    m->yy = (vy * xx - vx * xy) / det;
    m->x_offset = t.x - m->xx * r.x - m->xy * r.y;
    m->y_offset = t.y - m->yx * r.x - m->yy * r.y;
    if (!touch_cal_mapping_valid(m)) return false;
    for (int i = 0; i < 4; ++i) {
        touch_cal_point_t p = touch_cal_map(m, raw[i]);
        if (fabsf(p.x - target[i].x) > TOUCH_CAL_MAX_RESIDUAL_PX ||
            fabsf(p.y - target[i].y) > TOUCH_CAL_MAX_RESIDUAL_PX) return false;
    }
    return true;
}
