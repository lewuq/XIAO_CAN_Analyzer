#include "storage.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define SECTOR_SIZE 4096U
#define SNAP_SLOT_SIZE (2U * SECTOR_SIZE)
#define LOG_BASE DT_REG_ADDR(DT_NODELABEL(capture_log_partition))
#define LOG_SIZE DT_REG_SIZE(DT_NODELABEL(capture_log_partition))
#define SNAP_BASE DT_REG_ADDR(DT_NODELABEL(capture_snapshot_partition))
#define SNAP_SIZE DT_REG_SIZE(DT_NODELABEL(capture_snapshot_partition))
#define LOG_SECTORS (LOG_SIZE / SECTOR_SIZE)
#define LOG_MAGIC 0x34474f4cU
#define SNAP_MAGIC 0x34504e53U
#define BATCH_CAPACITY 53U

/* Commit the header last. A sector with a partial header or payload is ignored. */
struct block_header {
    uint32_t magic, seq, length, crc, session, count, reserved, header_crc;
};

struct stored_frame {
    uint32_t at_ms, id;
    uint8_t flags, length;
    uint16_t reserved;
    uint8_t data[64];
};

struct record_event {
    struct stored_frame frame;
    uint32_t profile;
};

BUILD_ASSERT(sizeof(struct stored_frame) == 76U);
BUILD_ASSERT(LOG_BASE % SECTOR_SIZE == 0U && LOG_SIZE % SECTOR_SIZE == 0U);
BUILD_ASSERT(SNAP_BASE % SECTOR_SIZE == 0U && SNAP_SIZE == 2U * SNAP_SLOT_SIZE);
BUILD_ASSERT(LOG_BASE + LOG_SIZE <= SNAP_BASE);
BUILD_ASSERT(sizeof(struct block_header) + BATCH_CAPACITY * sizeof(struct stored_frame)
             <= SECTOR_SIZE);
BUILD_ASSERT(sizeof(struct block_header) + sizeof(struct analyzer_model) <= SNAP_SLOT_SIZE);

static const struct device *const nor = DEVICE_DT_GET(DT_NODELABEL(ext_flash));
static struct stored_frame batch[BATCH_CAPACITY];
static uint32_t batch_profile;
static struct analyzer_model saved, pending;
static struct rate_view saved_rate, pending_rate;
static struct storage_status status;
static uint32_t log_seq, log_oldest, session, snap_seq;
static bool snapshot_pending;
static atomic_t queue_enabled, queue_dropped;
K_MUTEX_DEFINE(storage_lock);
K_MSGQ_DEFINE(record_queue, sizeof(struct record_event), 128, 4);
K_SEM_DEFINE(storage_wake, 0, 1);
K_THREAD_STACK_DEFINE(storage_stack, 3072);
static struct k_thread storage_thread;

static off_t log_address(uint32_t seq)
{
    return LOG_BASE + (off_t)((seq - 1U) % LOG_SECTORS) * SECTOR_SIZE;
}

static off_t snap_address(uint32_t seq)
{
    return SNAP_BASE + (off_t)((seq - 1U) % 2U) * SNAP_SLOT_SIZE;
}

static bool valid_header(const struct block_header *h, uint32_t magic)
{
    return h->magic == magic && h->seq != 0U &&
           h->header_crc == crc32_ieee((const uint8_t *)h, offsetof(struct block_header, header_crc));
}

static bool valid_batch(const struct block_header *h)
{
    return valid_header(h, LOG_MAGIC) && h->count > 0U &&
           h->count <= BATCH_CAPACITY && h->length == h->count * sizeof(struct stored_frame);
}

static int write_block(off_t addr, size_t erase_size, struct block_header *h,
                       const void *payload)
{
    h->crc = crc32_ieee(payload, h->length);
    h->header_crc = crc32_ieee((const uint8_t *)h, offsetof(struct block_header, header_crc));
    int ret = flash_erase(nor, addr, erase_size);
    if (ret == 0) {
        ret = flash_write(nor, addr + sizeof(*h), payload, h->length);
    }
    if (ret == 0) {
        ret = flash_write(nor, addr, h, sizeof(*h));
    }
    return ret;
}

static int scan(void)
{
    if (!device_is_ready(nor)) {
        return -ENODEV;
    }
    uint64_t size = 0;
    int ret = flash_get_size(nor, &size);
    if (ret < 0 || size < SNAP_BASE + SNAP_SIZE) {
        return ret < 0 ? ret : -ENOSPC;
    }

    struct block_header h;
    for (unsigned int i = 0; i < LOG_SECTORS; ++i) {
        off_t addr = LOG_BASE + (off_t)i * SECTOR_SIZE;
        ret = flash_read(nor, addr, &h, sizeof(h));
        if (ret < 0) { return ret; }
        if (valid_batch(&h) && addr == log_address(h.seq)) {
            if (h.seq > log_seq) {
                log_seq = h.seq;
                session = h.session;
            }
            if (log_oldest == 0U || h.seq < log_oldest) { log_oldest = h.seq; }
        }
        if ((i & 31U) == 31U) { k_msleep(1); }
    }
    ++session;
    for (unsigned int i = 0; i < 2U; ++i) {
        off_t addr = SNAP_BASE + (off_t)i * SNAP_SLOT_SIZE;
        ret = flash_read(nor, addr, &h, sizeof(h));
        if (ret < 0) { return ret; }
        if (!valid_header(&h, SNAP_MAGIC) || h.length != sizeof(saved) ||
            h.seq <= snap_seq || addr != snap_address(h.seq)) {
            continue;
        }
        ret = flash_read(nor, addr + sizeof(h), &pending, sizeof(pending));
        if (ret < 0) { return ret; }
        if (crc32_ieee((const uint8_t *)&pending, sizeof(pending)) == h.crc &&
            pending.id_count <= ANALYZER_IDS) {
            saved = pending;
            saved_rate = (struct rate_view){
                .active = rate_untag(h.reserved), .phase = RATE_SNAPSHOT,
                .nominal_ok = (h.count & 1U) != 0U,
                .data_ok = (h.count & 2U) != 0U,
            };
            snap_seq = h.seq;
        }
    }
    return 0;
}

static int flush(unsigned int count)
{
    if (log_seq == UINT32_MAX) { return -ENOSPC; }
    struct block_header h = {
        .magic = LOG_MAGIC, .seq = log_seq + 1U, .length = count * sizeof(batch[0]),
        .session = session, .count = count, .reserved = batch_profile,
    };
    int ret = write_block(log_address(h.seq), SECTOR_SIZE, &h, batch);
    k_mutex_lock(&storage_lock, K_FOREVER);
    if (ret == 0) {
        log_seq = h.seq;
        if (log_oldest == 0U) { log_oldest = h.seq; }
        if (h.seq - log_oldest >= LOG_SECTORS) {
            log_oldest = h.seq - LOG_SECTORS + 1U;
        }
        status.batches = log_seq - log_oldest + 1U;
    }
    k_mutex_unlock(&storage_lock);
    return ret;
}

static void save_if_requested(void)
{
    k_mutex_lock(&storage_lock, K_FOREVER);
    bool requested = snapshot_pending;
    k_mutex_unlock(&storage_lock);
    if (!requested) { return; }

    struct block_header h = {
        .magic = SNAP_MAGIC, .seq = snap_seq + 1U, .length = sizeof(pending),
        .session = session, .reserved = rate_tag(pending_rate.active),
        .count = (pending_rate.nominal_ok || pending_rate.phase == RATE_MANUAL ? 1U : 0U)
                 | (pending_rate.data_ok ? 2U : 0U),
    };
    int ret = h.seq == 0U ? -ENOSPC :
              write_block(snap_address(h.seq), SNAP_SLOT_SIZE, &h, &pending);
    k_mutex_lock(&storage_lock, K_FOREVER);
    if (ret == 0) {
        saved = pending;
        saved_rate = pending_rate;
        saved_rate.nominal_ok = saved_rate.nominal_ok || saved_rate.phase == RATE_MANUAL;
        saved_rate.phase = RATE_SNAPSHOT;
        snap_seq = h.seq;
        status.snapshots = snap_seq;
    }
    status.snapshot_result = ret == 0 ? 1 : ret;
    snapshot_pending = false;
    k_mutex_unlock(&storage_lock);
}

static void writer(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    int ret = scan();
    k_mutex_lock(&storage_lock, K_FOREVER);
    status.error = ret;
    status.ready = ret == 0;
    status.batches = log_seq == 0U ? 0U : log_seq - log_oldest + 1U;
    status.snapshots = snap_seq;
    k_mutex_unlock(&storage_lock);
    if (ret < 0) {
        printk("Capture NOR unavailable: %d\n", ret);
        return;
    }

    unsigned int count = 0;
    uint32_t first_ms = 0;
    for (;;) {
        k_sem_take(&storage_wake, K_MSEC(50));
        save_if_requested();
        if (!atomic_get(&queue_enabled) && count == 0U &&
            k_msgq_num_used_get(&record_queue) == 0U) {
            k_mutex_lock(&storage_lock, K_FOREVER);
            status.flushing = false;
            k_mutex_unlock(&storage_lock);
            continue;
        }
        struct record_event next;
        bool boundary = false;
        while (count < BATCH_CAPACITY &&
               k_msgq_get(&record_queue, &next, K_NO_WAIT) == 0) {
            if (count == 0U) {
                first_ms = k_uptime_get_32();
                batch_profile = next.profile;
            } else if (next.profile != batch_profile) {
                boundary = true;
                break;
            }
            batch[count++] = next.frame;
        }
        if (boundary || count == BATCH_CAPACITY || (count != 0U &&
            (!atomic_get(&queue_enabled) || k_uptime_get_32() - first_ms >= 2000U))) {
            ret = flush(count);
            if (ret < 0) {
                k_mutex_lock(&storage_lock, K_FOREVER);
                atomic_clear(&queue_enabled);
                status.recording = status.flushing = false;
                status.error = ret;
                atomic_add(&queue_dropped, count + (boundary ? 1U : 0U) +
                           k_msgq_num_used_get(&record_queue));
                k_msgq_purge(&record_queue);
                k_mutex_unlock(&storage_lock);
                printk("Capture write failed: %d\n", ret);
            }
            count = 0;
            if (ret == 0 && boundary) {
                batch_profile = next.profile;
                batch[count++] = next.frame;
                first_ms = k_uptime_get_32();
            }
            if (k_msgq_num_used_get(&record_queue) != 0U) { k_sem_give(&storage_wake); }
        }
    }
}

void storage_start(void)
{
    k_thread_create(&storage_thread, storage_stack, K_THREAD_STACK_SIZEOF(storage_stack),
                    writer, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
}

void storage_status_get(struct storage_status *out)
{
    k_mutex_lock(&storage_lock, K_FOREVER);
    *out = status;
    out->dropped = (uint32_t)atomic_get(&queue_dropped);
    k_mutex_unlock(&storage_lock);
}

int storage_toggle(void)
{
    k_mutex_lock(&storage_lock, K_FOREVER);
    int ret = status.error;
    if (ret == 0 && (!status.ready || status.flushing)) { ret = -EAGAIN; }
    if (ret == 0) {
        status.recording = !status.recording;
        status.flushing = !status.recording;
        atomic_set(&queue_enabled, status.recording);
    }
    k_mutex_unlock(&storage_lock);
    k_sem_give(&storage_wake);
    return ret;
}

void storage_frame(const struct rx_event *event, uint32_t profile)
{
    if (!atomic_get(&queue_enabled)) { return; }
    struct record_event record = {
        .frame = { .at_ms = event->at_ms, .id = event->id,
                   .flags = event->flags, .length = event->length },
        .profile = profile,
    };
    if (!(event->flags & FRAME_RTR)) {
        memcpy(record.frame.data, event->data, event->length);
    }
    /* Only the CAN consumer produces frames; synchronize with STOP and flush. */
    k_mutex_lock(&storage_lock, K_FOREVER);
    if (status.recording) {
        if (k_msgq_put(&record_queue, &record, K_NO_WAIT) == 0) {
            k_sem_give(&storage_wake);
        } else {
            atomic_inc(&queue_dropped);
        }
    }
    k_mutex_unlock(&storage_lock);
}

int storage_save(const struct analyzer_model *model, const struct rate_view *rate)
{
    k_mutex_lock(&storage_lock, K_FOREVER);
    int ret = !status.ready ? -ENODEV : snapshot_pending ? -EBUSY : 0;
    if (ret == 0) {
        pending = *model;
        pending_rate = *rate;
        snapshot_pending = true;
        status.snapshot_result = 0;
    }
    k_mutex_unlock(&storage_lock);
    if (ret == 0) { k_sem_give(&storage_wake); }
    return ret;
}

int storage_restore(struct analyzer_model *model, struct rate_view *rate)
{
    k_mutex_lock(&storage_lock, K_FOREVER);
    int ret = !status.ready ? -ENODEV : snap_seq == 0U ? -ENOENT : 0;
    if (ret == 0) {
        *model = saved;
        *rate = saved_rate;
    }
    k_mutex_unlock(&storage_lock);
    return ret;
}

static int capture_info(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    k_mutex_lock(&storage_lock, K_FOREVER);
    uint32_t first = log_oldest, last = log_seq, snaps = snap_seq;
    int error = status.error;
    k_mutex_unlock(&storage_lock);
    shell_print(sh, "Batches %u..%u, snapshot %u, log drops %u, error %d",
                first, last, snaps, (uint32_t)atomic_get(&queue_dropped), error);
    return 0;
}

static int capture_dump(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    uint32_t seq;
    char *end;
    errno = 0;
    unsigned long value = strtoul(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || !value ||
        value > UINT32_MAX || argv[1][0] == '-') {
        return -EINVAL;
    }
    seq = (uint32_t)value;
    struct storage_status present;
    storage_status_get(&present);
    if (!present.ready) { return -ENODEV; }

    static struct stored_frame export_frames[BATCH_CAPACITY];
    struct block_header h;
    off_t addr = log_address(seq);
    int ret = flash_read(nor, addr, &h, sizeof(h));
    if (ret < 0) { return ret; }
    if (!valid_batch(&h) || h.seq != seq) { return -ENOENT; }
    ret = flash_read(nor, addr + sizeof(h), export_frames, h.length);
    if (ret < 0) { return ret; }
    if (crc32_ieee((const uint8_t *)export_frames, h.length) != h.crc) { return -EIO; }

    struct rate_pair profile = rate_untag(h.reserved);
    shell_print(sh, "batch,session,uptime_ms,id_hex,flags_hex,length,data_hex,nominal_kbps,data_config_kbps");
    for (unsigned int i = 0; i < h.count; ++i) {
        const struct stored_frame *f = &export_frames[i];
        char data[129];
        size_t n = (f->flags & FRAME_RTR) ? 0U : MIN(f->length, sizeof(f->data));
        bin2hex(f->data, n, data, sizeof(data));
        data[n * 2U] = '\0';
        shell_print(sh, "%u,%u,%u,%08x,%02x,%u,%s,%u,%u",
                    h.seq, h.session, f->at_ms, f->id, f->flags, f->length, data,
                    profile.nominal, profile.data);
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(capture_commands,
    SHELL_CMD_ARG(info, NULL, "List committed capture batches", capture_info, 1, 0),
    SHELL_CMD_ARG(dump, NULL, "Export one committed batch as CSV: dump <batch>", capture_dump, 2, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(capture, &capture_commands, "External NOR CAN capture", NULL);
