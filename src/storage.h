#ifndef ANALYZER_STORAGE_H
#define ANALYZER_STORAGE_H

#include "model.h"
#include "rate.h"

struct storage_status {
    bool ready, recording, flushing;
    int error, snapshot_result;
    uint32_t batches, dropped, snapshots;
};

void storage_start(void);
void storage_status_get(struct storage_status *status);
int storage_toggle(void);
void storage_frame(const struct rx_event *event, uint32_t profile);
int storage_save(const struct analyzer_model *model, const struct rate_view *rate);
int storage_restore(struct analyzer_model *model, struct rate_view *rate);

#endif
