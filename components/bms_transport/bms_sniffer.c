#include "bms_sniffer.h"

#include <stdio.h>
#include "esp_log.h"
#include "jk_frame.h"

#define SNIFF_BUF           512     /* longer bursts are dumped in pieces */
#define SNIFF_PER_LINE      16
#define SNIFF_MAX_FRAMES    4       /* frame summaries collected per burst */

static const char *TAG = "sniffer";

static uint8_t s_buf[SNIFF_BUF];
static bool s_masked[SNIFF_BUF];
static size_t s_len;
static int64_t s_burst_us, s_prev_burst_us, s_prev_header_us;

/* JK frame tracking across bursts: position in the frame, -1 when outside one. */
static uint32_t s_shift;
static int s_pos = -1;
static uint8_t s_type, s_counter, s_sum;

typedef struct {
    uint8_t type, counter;
    bool sum_ok;
    long since_prev_ms;
} frame_summary_t;

static frame_summary_t s_frames[SNIFF_MAX_FRAMES];
static int s_frame_count;

/* Runs before the dump, so a sensitive byte is masked before it is ever printed. */
static bool track(uint8_t b)
{
    bool mask = false;
    if (s_pos >= 0) {
        ++s_pos;
        if (s_pos == JK_FRAME_TYPE_POS) s_type = b;
        if (s_pos == JK_FRAME_COUNTER_POS) s_counter = b;
        mask = jk_frame_byte_sensitive(s_type, s_pos);
        if (s_pos < JK_FRAME_SUM_POS) {
            s_sum += b;
        } else {
            if (s_frame_count < SNIFF_MAX_FRAMES) {
                long since = s_prev_header_us ? (long)((s_burst_us - s_prev_header_us) / 1000) : -1;
                s_frames[s_frame_count++] = (frame_summary_t){s_type, s_counter, s_sum == b, since};
            }
            s_prev_header_us = s_burst_us;
            s_pos = -1;
        }
    }
    s_shift = s_shift << 8 | b;
    if (s_shift == JK_FRAME_HEADER) {
        if (s_pos >= 0) ESP_LOGW(TAG, "header at byte %d of an unfinished frame: resync", s_pos);
        s_pos = JK_FRAME_TYPE_POS - 1;
        s_sum = (uint8_t)(0x55 + 0xAA + 0xEB + 0x90);
        s_type = 0;
    }
    return mask;
}

void bms_sniffer_feed(const uint8_t *data, size_t len, int64_t now_us)
{
    for (size_t i = 0; i < len; ++i) {
        if (s_len == SNIFF_BUF) bms_sniffer_flush();
        if (s_len == 0) s_burst_us = now_us;
        s_buf[s_len] = data[i];
        s_masked[s_len] = track(data[i]);
        if (s_masked[s_len]) s_buf[s_len] = 0;
        ++s_len;
    }
}

bool bms_sniffer_pending(void)
{
    return s_len > 0;
}

void bms_sniffer_flush(void)
{
    if (!s_len) return;
    if (s_prev_burst_us) {
        ESP_LOGI(TAG, "RX %u B, +%ld ms since previous burst", (unsigned)s_len,
                 (long)((s_burst_us - s_prev_burst_us) / 1000));
    } else {
        ESP_LOGI(TAG, "RX %u B, first burst", (unsigned)s_len);
    }

    char line[8 + SNIFF_PER_LINE * 3 + 2 + SNIFF_PER_LINE + 2];
    for (size_t off = 0; off < s_len; off += SNIFF_PER_LINE) {
        int n = snprintf(line, sizeof(line), "%04X:", (unsigned)off);
        for (size_t i = off; i < off + SNIFF_PER_LINE; ++i) {
            if (i >= s_len) n += snprintf(line + n, sizeof(line) - n, "   ");
            else if (s_masked[i]) n += snprintf(line + n, sizeof(line) - n, " **");
            else n += snprintf(line + n, sizeof(line) - n, " %02X", s_buf[i]);
        }
        n += snprintf(line + n, sizeof(line) - n, "  ");
        for (size_t i = off; i < off + SNIFF_PER_LINE && i < s_len; ++i) {
            char c = (char)s_buf[i];
            line[n++] = s_masked[i] ? '*' : (c >= 0x20 && c < 0x7F ? c : '.');
        }
        line[n] = '\0';
        ESP_LOGI(TAG, "%s", line);
    }
    for (int i = 0; i < s_frame_count; ++i) {
        const frame_summary_t *f = &s_frames[i];
        char since[40] = "first frame";
        if (f->since_prev_ms >= 0) snprintf(since, sizeof(since), "+%ld ms since previous frame", f->since_prev_ms);
        ESP_LOGI(TAG, "JK frame type %02X counter %02X, checksum %s, %s", f->type, f->counter,
                 f->sum_ok ? "OK" : "BAD", since);
    }
    s_frame_count = 0;
    s_prev_burst_us = s_burst_us;
    s_len = 0;
}
