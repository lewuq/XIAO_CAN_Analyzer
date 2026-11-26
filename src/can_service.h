#ifndef ANALYZER_CAN_SERVICE_H
#define ANALYZER_CAN_SERVICE_H
#include "model.h"
#include "rate.h"
int analyzer_start(void);
void analyzer_select_rate(size_t preset);
void analyzer_snapshot(struct analyzer_model *out, struct rate_view *view);
void analyzer_clear(void);
#endif
