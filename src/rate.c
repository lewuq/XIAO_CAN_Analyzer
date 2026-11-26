#include "rate.h"
#include <string.h>

#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define DWELL_MS 300U
#define SLOW_DWELL_MS 12000U

static const uint16_t nominal_rates[] = { 500, 250, 125, 1000, 100, 50, 800, 20, 10 };
static const uint16_t data_rates[] = { 2000, 4000, 1000, 8000, 3000, 6000, 500 };
_Static_assert(LENGTH(data_rates) <= 8U, "Data trial mask is eight bits");

static const struct rate_pair presets[] = {
    {0, 0}, /* AUTO */
    {500, 2000}, {250, 2000}, {125, 2000}, {1000, 2000},
    {500, 4000}, {1000, 4000}, {500, 1000}, {250, 1000},
    {500, 8000}, {1000, 8000}, {100, 2000}, {50, 2000},
    {800, 2000}, {20, 2000}, {10, 2000},
};

size_t rate_preset_count(void)
{
    return LENGTH(presets);
}

struct rate_pair rate_preset(size_t index)
{
    return index < LENGTH(presets) ? presets[index] : presets[0];
}

static void choose(struct rate_search *s)
{
    s->candidate = (struct rate_pair){ nominal_rates[s->nominal_index], data_rates[s->data_index] };
}

void rate_search_reset(struct rate_search *s, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    choose(s);
    rate_search_trial(s, now);
}

void rate_search_trial(struct rate_search *s, uint32_t now)
{
    s->started_ms = now;
    s->frames = s->brs = 0;
}

void rate_search_frame(struct rate_search *s, bool brs)
{
    if (s->frames < RATE_PROOF_FRAMES) { ++s->frames; }
    if (brs && s->brs < RATE_PROOF_FRAMES) { ++s->brs; }
}

enum scan_result rate_search_poll(struct rate_search *s, uint32_t now, bool rejected)
{
    if (!rejected && s->brs >= RATE_PROOF_FRAMES) {
        s->best = s->candidate;
        s->best_brs = s->brs;
        return SCAN_LOCK;
    }
    /* Any evidence of a slow bus gets extra time. After nominal lock, only
     * BRS frames extend a trial; Classic or FD without BRS cannot prove data.
     */
    bool partial = (s->brs > 0U) ||
                   (!s->anchored && s->frames > 0U && s->frames < RATE_PROOF_FRAMES);
    if (!rejected && now - s->started_ms < (partial ? SLOW_DWELL_MS : DWELL_MS)) {
        return SCAN_PENDING;
    }

    if (!rejected && !s->anchored && s->frames >= RATE_PROOF_FRAMES) {
        s->anchored = true;
        s->best = s->candidate;
    }
    if (s->anchored) {
        if (!rejected && s->brs > s->best_brs) {
            s->best = s->candidate;
            s->best_brs = s->brs;
        }
        s->tried_data |= (uint8_t)(1U << s->data_index);
        for (uint8_t i = 0; i < LENGTH(data_rates); ++i) {
            if (!(s->tried_data & (1U << i)) && data_rates[i] >= s->best.nominal) {
                s->data_index = i;
                choose(s);
                return SCAN_NEXT;
            }
        }
        return SCAN_LOCK; /* Nominal is proven; data remains unobserved/unconfirmed. */
    }

    do {
        if (++s->nominal_index == LENGTH(nominal_rates)) {
            s->nominal_index = 0;
            if (++s->data_index == LENGTH(data_rates)) {
                return SCAN_EXHAUSTED;
            }
        }
        choose(s);
    } while (s->candidate.data < s->candidate.nominal);
    return SCAN_NEXT;
}
