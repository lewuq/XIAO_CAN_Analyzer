#ifndef ANALYZER_MODEL_H
#define ANALYZER_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#define ANALYZER_IDS 64U
#define FRAME_EXT 0x01U
#define FRAME_RTR 0x02U
#define FRAME_FD  0x04U
#define FRAME_BRS 0x08U

enum bus_state { BUS_ACTIVE, BUS_WARNING, BUS_PASSIVE, BUS_OFF, BUS_STOPPED, BUS_INIT_ERROR };
struct rx_event {
    uint32_t id, at_ms, epoch;
    uint8_t flags, length, data[64];
};
struct id_record {
    uint32_t id, count, seen_ms;
    uint8_t flags, length, data[64];
};
struct analyzer_model {
    uint32_t total, rate, queue_drops, overruns, fd_total, classic_total;
    uint32_t started_ms, now_ms, rate_start_ms, rate_frames;
    uint16_t id_count;
    uint8_t rx_errors, tx_errors;
    bool cache_limited;
    enum bus_state bus;
    int init_error;
    struct id_record ids[ANALYZER_IDS];
};
void model_init(struct analyzer_model *m, uint32_t now);
void model_clear(struct analyzer_model *m, uint32_t now);
bool model_receive(struct analyzer_model *m, const struct rx_event *e);
void model_tick(struct analyzer_model *m, uint32_t now);
#endif
