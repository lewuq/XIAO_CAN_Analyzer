#include "model.h"
#include <limits.h>
#include <string.h>

static void increment(uint32_t *n) { if (*n != UINT32_MAX) { ++*n; } }

void model_init(struct analyzer_model *m, uint32_t now)
{
    memset(m, 0, sizeof(*m));
    m->bus = BUS_STOPPED;
    m->started_ms = m->rate_start_ms = m->now_ms = now;
}

void model_clear(struct analyzer_model *m, uint32_t now)
{
    enum bus_state bus = m->bus;
    int error = m->init_error;
    uint8_t rx = m->rx_errors, tx = m->tx_errors;
    model_init(m, now);
    m->bus = bus;
    m->init_error = error;
    m->rx_errors = rx;
    m->tx_errors = tx;
}

bool model_receive(struct analyzer_model *m, const struct rx_event *e)
{
    if (e->length > 64U || e->id > ((e->flags & FRAME_EXT) ? 0x1fffffffU : 0x7ffU) ||
        (!(e->flags & FRAME_FD) && e->length > 8U) ||
        ((e->flags & FRAME_FD) && (e->flags & FRAME_RTR))) {
        return false;
    }
    increment(&m->total);
    increment(&m->rate_frames);
    increment((e->flags & FRAME_FD) ? &m->fd_total : &m->classic_total);
    unsigned int i;
    for (i = 0; i < m->id_count; ++i) {
        if (m->ids[i].id == e->id &&
            ((m->ids[i].flags ^ e->flags) & FRAME_EXT) == 0U) { break; }
    }
    if (i == m->id_count) {
        if (m->id_count == ANALYZER_IDS) {
            m->cache_limited = true;
            return true; /* totals still include untracked IDs */
        }
        ++m->id_count;
    }
    struct id_record *r = &m->ids[i];
    r->id = e->id;
    r->flags = e->flags;
    r->length = e->length;
    r->seen_ms = e->at_ms;
    increment(&r->count);
    memset(r->data, 0, sizeof(r->data));
    if (!(e->flags & FRAME_RTR)) { memcpy(r->data, e->data, e->length); }
    return true;
}

void model_tick(struct analyzer_model *m, uint32_t now)
{
    uint32_t elapsed = now - m->rate_start_ms;
    m->now_ms = now;
    if (elapsed >= 1000U) {
        uint64_t rate = (uint64_t)m->rate_frames * 1000U / elapsed;
        m->rate = rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
        m->rate_frames = 0;
        m->rate_start_ms = now;
    }
}
