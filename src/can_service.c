#include "can_service.h"
#include "storage.h"
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define RETRY_MS 1500U
static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(fdcan2));
K_MUTEX_DEFINE(model_lock);
K_MSGQ_DEFINE(rx_queue, sizeof(struct rx_event), 128, 4);
K_THREAD_STACK_DEFINE(rx_stack, 2560);
static struct k_thread rx_thread;
static struct analyzer_model model;
static struct rate_view visible_rate;
static atomic_t queue_drops, session_epoch, rx_enabled, preset_request;
static uint32_t overrun_base;
/* Only the receiver worker owns start/stop and timing after analyzer_start. */
static bool started;

BUILD_ASSERT(CAN_FRAME_IDE == FRAME_EXT && CAN_FRAME_RTR == FRAME_RTR &&
             CAN_FRAME_FDF == FRAME_FD && CAN_FRAME_BRS == FRAME_BRS);
BUILD_ASSERT(CAN_MAX_DLEN == 64);

static void receive(const struct device *dev, struct can_frame *frame, void *user)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user);
    if (!atomic_get(&rx_enabled) || frame->dlc > 15U) {
        return;
    }
    struct rx_event event = {
        .id = frame->id,
        .at_ms = k_uptime_get_32(),
        .epoch = (uint32_t)atomic_get(&session_epoch),
        .flags = frame->flags & (FRAME_EXT | FRAME_RTR | FRAME_FD | FRAME_BRS),
        .length = (frame->flags & CAN_FRAME_FDF) ? can_dlc_to_bytes(frame->dlc) :
                  MIN(frame->dlc, 8U),
    };
    if (!(frame->flags & CAN_FRAME_RTR)) {
        memcpy(event.data, frame->data, event.length);
    }
    if (k_msgq_put(&rx_queue, &event, K_NO_WAIT) != 0) {
        atomic_inc(&queue_drops);
    }
}

static enum bus_state convert_state(enum can_state state)
{
    switch (state) {
    case CAN_STATE_ERROR_ACTIVE: return BUS_ACTIVE;
    case CAN_STATE_ERROR_WARNING: return BUS_WARNING;
    case CAN_STATE_ERROR_PASSIVE: return BUS_PASSIVE;
    case CAN_STATE_BUS_OFF: return BUS_OFF;
    default: return BUS_STOPPED;
    }
}

static int setup_bus(void)
{
    if (!device_is_ready(bus)) { return -ENODEV; }
    can_mode_t caps;
    int ret = can_get_capabilities(bus, &caps);
    if (ret < 0) { return ret; }
    const can_mode_t mode = CAN_MODE_FD | CAN_MODE_LISTENONLY;
    if ((caps & mode) != mode) { return -ENOTSUP; }

    ret = can_set_mode(bus, mode);
    if (ret < 0) { return ret; }
    const struct can_filter standard = { .id = 0, .mask = 0, .flags = 0 };
    const struct can_filter extended = { .id = 0, .mask = 0, .flags = CAN_FILTER_IDE };
    int id = can_add_rx_filter(bus, receive, NULL, &standard);
    if (id < 0) { return id; }
    ret = can_add_rx_filter(bus, receive, NULL, &extended);
    if (ret < 0) { can_remove_rx_filter(bus, id); }
    return ret < 0 ? ret : 0;
}

static int stop_bus(void)
{
    atomic_clear(&rx_enabled);
    if (started) {
        int ret = can_stop(bus);
        if (ret < 0 && ret != -EALREADY) { return ret; }
        started = false;
    }
    atomic_inc(&session_epoch);
    k_msgq_purge(&rx_queue);
    return 0;
}

static int apply_pair(struct rate_pair pair)
{
    int ret = stop_bus();
    if (ret < 0) { return ret; }

    struct can_timing nominal, data;
    ret = can_calc_timing(bus, &nominal, (uint32_t)pair.nominal * 1000U, 0);
    if (ret < 0) { return ret; }
    ret = can_calc_timing_data(bus, &data, (uint32_t)pair.data * 1000U, 750);
    if (ret < 0) { return ret; }
    ret = can_set_timing(bus, &nominal);
    if (ret < 0) { return ret; }
    ret = can_set_timing_data(bus, &data);
    if (ret < 0) { return ret; }

    ret = can_start(bus);
    if (ret < 0) { return ret; }
    started = true;
    atomic_clear(&queue_drops);
    k_mutex_lock(&model_lock, K_FOREVER);
    overrun_base = can_stats_get_rx_overruns(bus);
    k_mutex_unlock(&model_lock);
    atomic_set(&rx_enabled, 1);
    return 0;
}

static bool capturing(enum rate_phase phase)
{
    return phase == RATE_LOCKED || phase == RATE_MANUAL;
}

static void publish_status(const struct rate_view *view, int error, uint32_t now)
{
    k_mutex_lock(&model_lock, K_FOREVER);
    visible_rate = *view;
    model.init_error = error;
    model_tick(&model, now);
    if (capturing(view->phase) && started) {
        enum can_state state;
        struct can_bus_err_cnt errors;
        if (can_get_state(bus, &state, &errors) == 0) {
            model.bus = convert_state(state);
            model.rx_errors = errors.rx_err_cnt;
            model.tx_errors = errors.tx_err_cnt;
        }
        model.queue_drops = (uint32_t)atomic_get(&queue_drops);
        model.overruns = can_stats_get_rx_overruns(bus) - overrun_base;
    } else {
        model.bus = error ? BUS_INIT_ERROR : BUS_STOPPED;
    }
    k_mutex_unlock(&model_lock);
}

static int start_trial(struct rate_search *search, struct rate_view *view)
{
    int ret = apply_pair(search->candidate);
    rate_search_trial(search, k_uptime_get_32());
    *view = (struct rate_view){
        .active = search->candidate,
        .phase = search->anchored ? RATE_SCAN_DATA : RATE_SCAN,
        .nominal_ok = search->anchored,
    };
    return ret;
}

static bool unsupported(int ret)
{
    return ret == -EINVAL || ret == -ENOTSUP;
}

static void consume(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    struct rate_search search = {0};
    struct rate_view view = {0};
    uint32_t retry_since = 0, last_update = 0, receive_since = 0;
    unsigned int verified_brs = 0;
    int error = 0;
    bool rejected = false;

    for (;;) {
        uint32_t now = k_uptime_get_32();
        int request = atomic_set(&preset_request, 0);
        bool update = false;
        if (request != 0) {
            k_mutex_lock(&model_lock, K_FOREVER);
            model_init(&model, now);
            k_mutex_unlock(&model_lock);
            verified_brs = 0;
            error = 0;
            if (request == 1) {
                rate_search_reset(&search, now);
                error = start_trial(&search, &view);
            } else {
                struct rate_pair pair = rate_preset((size_t)request - 1U);
                view = (struct rate_view){ .active = pair, .phase = RATE_MANUAL };
                error = apply_pair(pair);
                if (error == 0) {
                    printk("FDCAN2 MANUAL %uK/%uK LISTEN\n", pair.nominal, pair.data);
                }
            }
            receive_since = k_uptime_get_32();
            update = true;
        } else if (view.phase == RATE_WAIT && now - retry_since >= RETRY_MS) {
            rate_search_reset(&search, now);
            error = start_trial(&search, &view);
            receive_since = k_uptime_get_32();
            update = true;
        }

        bool scanning = view.phase == RATE_SCAN || view.phase == RATE_SCAN_DATA;
        rejected = scanning && unsupported(error);
        if (error < 0 && !rejected && view.phase != RATE_ERROR) {
            view.phase = RATE_ERROR;
            atomic_clear(&rx_enabled);
            printk("FDCAN2 configuration failed: %d\n", error);
            update = true;
        }
        if (update) {
            publish_status(&view, rejected ? 0 : error, k_uptime_get_32());
        }

        struct rx_event event;
        int ret = k_msgq_get(&rx_queue, &event, K_MSEC(20));
        now = k_uptime_get_32();
        if (ret == 0 && event.epoch == (uint32_t)atomic_get(&session_epoch) &&
            (uint32_t)(event.at_ms - receive_since) < INT32_MAX) {
            bool brs = (event.flags & (FRAME_FD | FRAME_BRS)) == (FRAME_FD | FRAME_BRS);
            if (scanning && !rejected && error == 0) {
                rate_search_frame(&search, brs);
            } else if (capturing(view.phase)) {
                k_mutex_lock(&model_lock, K_FOREVER);
                bool accepted = model_receive(&model, &event);
                k_mutex_unlock(&model_lock);
                if (accepted) {
                    view.saw_frame = view.nominal_ok = true;
                    view.last_frame_ms = event.at_ms;
                    if (brs && verified_brs < RATE_PROOF_FRAMES &&
                        ++verified_brs == RATE_PROOF_FRAMES) {
                        view.data_ok = true;
                        update = true;
                    }
                    storage_frame(&event, rate_tag(view.active));
                }
            }
        }

        if (scanning && (error == 0 || rejected)) {
            enum scan_result result = rate_search_poll(&search, now, rejected);
            switch (result) {
            case SCAN_NEXT:
                error = start_trial(&search, &view);
                receive_since = k_uptime_get_32();
                update = true;
                break;
            case SCAN_LOCK:
                if (!started || rate_tag(search.best) != rate_tag(view.active)) {
                    error = apply_pair(search.best);
                    receive_since = k_uptime_get_32();
                }
                verified_brs = search.best_brs;
                view = (struct rate_view){
                    .active = search.best, .phase = RATE_LOCKED, .nominal_ok = true,
                    .data_ok = verified_brs >= RATE_PROOF_FRAMES,
                    .last_frame_ms = now, .saw_frame = true,
                };
                if (error == 0) {
                    printk("FDCAN2 AUTO LOCK NOM %uK, DATA %uK (%s)\n",
                           view.active.nominal, view.active.data,
                           view.data_ok ? "BRS verified" : "not verified");
                    /* The capture session begins here, after all probe traffic. */
                    k_mutex_lock(&model_lock, K_FOREVER);
                    model_init(&model, now);
                    overrun_base = can_stats_get_rx_overruns(bus);
                    atomic_clear(&queue_drops);
                    k_mutex_unlock(&model_lock);
                }
                update = true;
                break;
            case SCAN_EXHAUSTED:
                error = stop_bus();
                view = (struct rate_view){ .phase = RATE_WAIT };
                retry_since = now;
                update = true;
                break;
            case SCAN_PENDING:
                break;
            }
        }

        if (update || now - last_update >= 100U) {
            if (error < 0 && !(view.phase == RATE_SCAN || view.phase == RATE_SCAN_DATA)) {
                view.phase = RATE_ERROR;
            }
            publish_status(&view, view.phase == RATE_ERROR ? error : 0, now);
            last_update = now;
        }
    }
}

int analyzer_start(void)
{
    model_init(&model, k_uptime_get_32());
    int ret = setup_bus();
    if (ret < 0) {
        visible_rate.phase = RATE_ERROR;
        model.bus = BUS_INIT_ERROR;
        model.init_error = ret;
        return ret;
    }
    atomic_set(&preset_request, 1); /* AUTO at boot. */
    k_thread_create(&rx_thread, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
                    consume, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
    return 0;
}

void analyzer_select_rate(size_t preset)
{
    if (preset < rate_preset_count()) {
        atomic_set(&preset_request, (atomic_val_t)preset + 1);
    }
}

void analyzer_snapshot(struct analyzer_model *out, struct rate_view *view)
{
    k_mutex_lock(&model_lock, K_FOREVER);
    *out = model;
    *view = visible_rate;
    out->now_ms = k_uptime_get_32();
    k_mutex_unlock(&model_lock);
}

void analyzer_clear(void)
{
    k_mutex_lock(&model_lock, K_FOREVER);
    atomic_inc(&session_epoch);
    atomic_clear(&queue_drops);
    model_clear(&model, k_uptime_get_32());
    if (device_is_ready(bus)) {
        overrun_base = can_stats_get_rx_overruns(bus);
    }
    k_mutex_unlock(&model_lock);
}
