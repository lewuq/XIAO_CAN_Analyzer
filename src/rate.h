#ifndef ANALYZER_RATE_H
#define ANALYZER_RATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RATE_PROOF_FRAMES 10U
#define RATE_IDLE_MS 3000U

/* All presets use integral kbit/s values. Zero means unspecified. */
struct rate_pair {
    uint16_t nominal, data;
};

enum rate_phase { RATE_SCAN, RATE_SCAN_DATA, RATE_WAIT, RATE_LOCKED, RATE_MANUAL,
                  RATE_ERROR, RATE_SNAPSHOT };

struct rate_view {
    struct rate_pair active;
    enum rate_phase phase;
    uint32_t last_frame_ms;
    bool nominal_ok, data_ok, saw_frame;
};

struct rate_search {
    struct rate_pair candidate, best;
    uint32_t started_ms;
    uint8_t nominal_index, data_index, tried_data;
    uint8_t frames, brs, best_brs;
    bool anchored;
};

enum scan_result { SCAN_PENDING, SCAN_NEXT, SCAN_LOCK, SCAN_EXHAUSTED };

size_t rate_preset_count(void);
struct rate_pair rate_preset(size_t index);
void rate_search_reset(struct rate_search *s, uint32_t now);
void rate_search_trial(struct rate_search *s, uint32_t now);
void rate_search_frame(struct rate_search *s, bool brs);
enum scan_result rate_search_poll(struct rate_search *s, uint32_t now, bool rejected);

static inline uint32_t rate_tag(struct rate_pair pair)
{
    return ((uint32_t)pair.nominal << 16) | pair.data;
}

static inline struct rate_pair rate_untag(uint32_t tag)
{
    return (struct rate_pair){ (uint16_t)(tag >> 16), (uint16_t)tag };
}

#endif
