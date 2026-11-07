#include "bms_sniffer.h"

#include <stdio.h>
#include "esp_log.h"

#define SNIFF_BUF           512     /* longer bursts are dumped in pieces */
#define SNIFF_PER_LINE      16
#define SNIFF_MAX_FRAMES    4       /* frame summaries collected per burst */

static const char *TAG = "sniffer";

static uint8_t s_buf[SNIFF_BUF];
static bool s_masked[SNIFF_BUF];
static size_t s_len;
static int64_t s_burst_us, s_prev_burst_us, s_prev_header_us;

typedef struct {
    uint8_t type, counter;
    bool sum_ok;
    long since_prev_ms;
} frame_summary_t;

static frame_summary_t s_frames[SNIFF_MAX_FRAMES];
static int s_frame_count;

void bms_sniffer_feed(const uint8_t *data, const bool *masked, size_t len, int64_t now_us)
{
    for (size_t i = 0; i < len; ++i) {
        if (s_len == SNIFF_BUF) bms_sniffer_flush();
        if (s_len == 0) s_burst_us = now_us;
        s_masked[s_len] = masked[i];
        s_buf[s_len] = masked[i] ? 0 : data[i];
        ++s_len;
    }
}

void bms_sniffer_frame(uint8_t type, uint8_t counter, bool sum_ok)
{
    if (s_frame_count < SNIFF_MAX_FRAMES) {
        long since = s_prev_header_us ? (long)((s_burst_us - s_prev_header_us) / 1000) : -1;
        s_frames[s_frame_count++] = (frame_summary_t){type, counter, sum_ok, since};
    }
    s_prev_header_us = s_burst_us;
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
