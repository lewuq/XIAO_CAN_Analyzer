"""Host check of the firmware's NOR persistence using an in-memory flash."""
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

source = Path(__file__).resolve().parents[1] / "src/storage.c"
headers = (
    "device.h", "drivers/flash.h", "kernel.h", "shell/shell.h",
    "sys/atomic.h", "sys/crc.h", "sys/printk.h", "sys/util.h",
)
with TemporaryDirectory() as tmp:
    root = Path(tmp)
    for name in headers:
        path = root / "zephyr" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("")
    c = root / "test.c"
    c.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#define ARG_UNUSED(a) ((void)(a))
#define BUILD_ASSERT(condition,...) _Static_assert(condition,#condition)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define DT_NODELABEL(x) x
#define REG_ADDR_(x) addr_##x
#define DT_REG_ADDR(x) REG_ADDR_(x)
#define REG_SIZE_(x) size_##x
#define DT_REG_SIZE(x) REG_SIZE_(x)
#define addr_capture_log_partition 0x800000U
#define size_capture_log_partition 0x7fc000U
#define addr_capture_snapshot_partition 0xffc000U
#define size_capture_snapshot_partition 0x4000U
struct device { int unused; };
static struct device fake;
#define DEVICE_DT_GET(x) (&fake)
typedef int atomic_t;
struct k_thread { int unused; };
struct shell { int unused; };
static jmp_buf writer_done;
static unsigned int iterations, input_index, input_count;
static unsigned char input_queue[4][80];
static int fake_get(void *out)
{
    if (input_index == input_count) { return -1; }
    memcpy(out, input_queue[input_index++], sizeof(input_queue[0]));
    return 0;
}
#define K_MUTEX_DEFINE(name) int name
#define K_MSGQ_DEFINE(name,...) int name
#define K_SEM_DEFINE(name,...) int name
#define K_THREAD_STACK_DEFINE(name,n) char name[n]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define K_FOREVER 0
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define k_thread_create(...) ((void)0)
#define k_mutex_lock(...) ((void)0)
#define k_mutex_unlock(...) ((void)0)
#define k_sem_take(...) (++iterations > 8 ? (longjmp(writer_done, 1), 0) : 0)
#define k_sem_give(...) ((void)0)
#define k_msleep(...) ((void)0)
#define k_msgq_num_used_get(...) (input_count - input_index)
#define k_msgq_get(q,d,t) fake_get(d)
#define k_msgq_put(...) 0
#define k_msgq_purge(...) ((void)0)
#define k_uptime_get_32(...) 0U
#define atomic_clear(x) (*(x)=0)
#define atomic_get(x) (*(x))
#define atomic_set(x,n) (*(x)=(n))
#define atomic_add(x,n) (*(x)+=(n))
#define atomic_inc(x) ((*(x))++)
#define SHELL_STATIC_SUBCMD_SET_CREATE(...)
#define SHELL_CMD_REGISTER(...)
#define shell_print(...) ((void)0)
#define printk(...) ((void)0)
#define bin2hex(...) ((void)0)

static uint8_t bytes[0x1000000];
static unsigned int writes, fail_write;
static bool device_is_ready(const struct device *d) { return d == &fake; }
static int flash_get_size(const struct device *d, uint64_t *size)
{ assert(d == &fake); *size = sizeof(bytes); return 0; }
static int flash_read(const struct device *d, off_t addr, void *p, size_t len)
{
    assert(d == &fake && addr >= 0 && (size_t)addr + len <= sizeof(bytes));
    memcpy(p, bytes + addr, len);
    return 0;
}
static int flash_erase(const struct device *d, off_t addr, size_t len)
{
    assert(d == &fake && addr >= 0x800000 && (size_t)addr + len <= sizeof(bytes));
    assert(addr % 4096 == 0 && len % 4096 == 0);
    memset(bytes + addr, 0xff, len);
    return 0;
}
static int flash_write(const struct device *d, off_t addr, const void *p, size_t len)
{
    assert(d == &fake && addr >= 0x800000 && (size_t)addr + len <= sizeof(bytes));
    bool fail = ++writes == fail_write;
    if (fail) { len /= 2; }
    const uint8_t *source = p;
    for (size_t i = 0; i < len; ++i) {
        assert((bytes[addr+i] & source[i]) == source[i]);
        bytes[addr+i] = source[i];
    }
    return fail ? -5 : 0;
}
static uint32_t crc32_ieee(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    while (length--) {
        crc ^= *data++;
        for (unsigned int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0);
        }
    }
    return ~crc;
}
#include "SOURCE"

static void reboot(void)
{
    log_seq = log_oldest = snap_seq = session = 0;
    memset(&saved, 0, sizeof(saved));
    assert(scan() == 0);
}

int main(void)
{
    memset(bytes, 0xff, sizeof(bytes));
    assert(scan() == 0 && log_seq == 0 && snap_seq == 0);
    status.ready = true;
    struct analyzer_model snap = { .total = 42, .id_count = 1 };
    struct rate_view rate = { .phase = RATE_SNAPSHOT }; /* old metadata, all zeros */
    assert(storage_save(&snap, &rate) == 0);
    save_if_requested();
    assert(snap_seq == 1);
    snap.total = 100;
    rate = (struct rate_view){ .active = {500, 4000}, .phase = RATE_LOCKED,
                              .nominal_ok = true, .data_ok = true };
    fail_write = writes + 2; /* lose power part way through the commit header */
    assert(storage_save(&snap, &rate) == 0);
    save_if_requested();
    assert(status.snapshot_result < 0);
    reboot();
    assert(storage_restore(&snap, &rate) == 0 && snap.total == 42);
    assert(rate.active.nominal == 0 && !rate.data_ok);
    snap.total = 100;
    rate = (struct rate_view){ .active = {500, 4000}, .phase = RATE_LOCKED,
                              .nominal_ok = true, .data_ok = true };
    assert(storage_save(&snap, &rate) == 0);
    save_if_requested();
    reboot();
    assert(storage_restore(&snap, &rate) == 0 && snap.total == 100);
    assert(rate.phase == RATE_SNAPSHOT && rate.active.nominal == 500 &&
           rate.active.data == 4000 && rate.data_ok);

    batch[0].id = 0x123;
    batch[0].length = 64;
    batch_profile = rate_tag((struct rate_pair){500, 4000});
    assert(flush(1) == 0 && log_seq == 1);
    struct block_header h;
    assert(flash_read(nor, log_address(1), &h, sizeof(h)) == 0);
    assert(rate_untag(h.reserved).data == 4000);
    char *args[] = {"dump", "1"};
    assert(capture_dump(NULL, 2, args) == 0);
    fail_write = writes + 1; /* torn payload leaves no committed header */
    assert(flush(1) < 0);
    reboot();
    assert(log_seq == 1 && log_oldest == 1);
    assert(flush(1) == 0 && log_seq == 2);
    bytes[log_address(2) + sizeof(struct block_header)] ^= 1;
    args[1] = "2";
    assert(capture_dump(NULL, 2, args) == -EIO);
    log_seq = LOG_SECTORS;
    assert(flush(1) == 0 && log_oldest == 2);
    args[1] = "1";
    assert(capture_dump(NULL, 2, args) == -ENOENT);
    for (size_t i = 0; i < 0x800000; ++i) { assert(bytes[i] == 0xff); }

    /* Run the actual writer across three consecutive timing configurations. */
    memset(bytes, 0xff, sizeof(bytes));
    reboot();
    const struct rate_pair profiles[] = {{500, 2000}, {500, 2000}, {250, 2000}, {500, 4000}};
    _Static_assert(sizeof(struct record_event) == sizeof(input_queue[0]), "mock queue ABI");
    input_count = 4;
    for (unsigned int i = 0; i < input_count; ++i) {
        struct record_event entry = { .frame.id = 0x100 + i, .profile = rate_tag(profiles[i]) };
        memcpy(input_queue[i], &entry, sizeof(entry));
    }
    if (setjmp(writer_done) == 0) { writer(NULL, NULL, NULL); }
    assert(log_seq == 3);
    for (unsigned int i = 1; i <= 3; ++i) {
        assert(flash_read(nor, log_address(i), &h, sizeof(h)) == 0);
        assert(h.count == (i == 1 ? 2 : 1) && h.reserved == rate_tag(profiles[i]));
        struct stored_frame frame;
        assert(flash_read(nor, log_address(i) + sizeof(h), &frame, sizeof(frame)) == 0);
        assert(frame.id == (i == 1 ? 0x100 : 0x100 + i));
    }
    puts("PASS: NOR recovery, CRC, log wrap, legacy snapshots and rate-separated batches");
}
'''.replace("SOURCE", source.as_posix()), encoding="utf-8")
    exe = root / "nor_tests.exe"
    subprocess.run([sys.argv[1], "-std=c11", "-O2", "-I", str(root), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
