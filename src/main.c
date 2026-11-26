#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include "button.h"
#include "can_service.h"
#include "display.h"
#include "ui.h"

#define BUTTON_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec keys[] = {
    GPIO_DT_SPEC_GET(BUTTON_NODE, user_button_gpios),
    GPIO_DT_SPEC_GET(BUTTON_NODE, boot_button_gpios),
};
struct key_event { uint8_t key; enum press_event press; };
K_MSGQ_DEFINE(key_queue,sizeof(struct key_event),16,4);
K_THREAD_STACK_DEFINE(button_stack,1024);
static struct k_thread button_thread;
static struct canvas canvases[2];
static struct analyzer_model live, held;
static struct ui_state state;
BUILD_ASSERT(GFX_BYTES == DISPLAY_BYTES, "Display and UI framebuffer sizes must match");

static void poll_buttons(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    struct button_state buttons[2];
    uint32_t now=k_uptime_get_32();
    for (unsigned int i=0; i<2U; ++i) { button_init(&buttons[i],gpio_pin_get_dt(&keys[i])>0,now); }
    for (;;) {
        now=k_uptime_get_32();
        for (unsigned int i=0; i<2U; ++i) {
            int down=gpio_pin_get_dt(&keys[i]);
            if (down<0) { continue; }
            enum press_event press=button_step(&buttons[i],down!=0,now);
            if (press!=PRESS_NONE) {
                struct key_event e={ .key=i, .press=press };
                (void)k_msgq_put(&key_queue,&e,K_NO_WAIT);
            }
        }
        k_msleep(5);
    }
}
static int buttons_start(void)
{
    for (unsigned int i=0; i<2U; ++i) {
        if (!gpio_is_ready_dt(&keys[i])) { return -1; }
        int ret=gpio_pin_configure_dt(&keys[i],GPIO_INPUT);
        if (ret<0) { return ret; }
    }
    k_thread_create(&button_thread,button_stack,K_THREAD_STACK_SIZEOF(button_stack),
                    poll_buttons,NULL,NULL,NULL,5,0,K_NO_WAIT);
    return 0;
}
static void key_action(const struct key_event *event)
{
    const struct analyzer_model *view=state.holding?&held:&live;
    enum ui_action action=ui_button(&state,view,event->key,event->press,k_uptime_get_32());
    enum storage_note note = NOTE_NONE;
    if (action == UI_CAPTURE_HOLD && state.holding) {
        analyzer_snapshot(&held, &state.rate_held);
        int ret = storage_save(&held, &state.rate_held);
        if (ret < 0) { note = ret == -EBUSY ? NOTE_BUSY : NOTE_SAVE_ERROR; }
    }
    if (action==UI_CLEAR_STATS) {
        analyzer_clear();
        analyzer_snapshot(&live, &state.rate_live);
        if (state.holding) {
            held = live;
            state.rate_held = state.rate_live;
        }
    }
    if (action == UI_TOGGLE_RECORD && storage_toggle() < 0) {
        note = NOTE_BUSY;
    }
    if (action == UI_RESTORE) {
        int ret = storage_restore(&held, &state.rate_held);
        if (ret == 0) {
            state.holding = true;
            state.page = PAGE_OVERVIEW;
            state.selected = state.byte_page = 0;
            note = NOTE_RESTORED;
        } else {
            note = ret == -ENOENT ? NOTE_NO_SNAPSHOT : NOTE_BUSY;
        }
    }
    if (action == UI_APPLY_RATE) {
        state.holding = false;
        state.selected = state.byte_page = 0;
        analyzer_select_rate(state.rate_selection);
    }
    if (note != NOTE_NONE) {
        state.storage_note = note;
        state.storage_note_until = k_uptime_get_32() + 1200U;
    }
    printk("KEY %c %s; page=%u hold=%u%s\n",event->key?'B':'A',
           event->press==PRESS_LONG?"LONG":"SHORT",state.page+1U,state.holding,
           action==UI_CLEAR_STATS?" CLEARED":"");
}

int main(void)
{
    printk("\nCAN ANALYZER v5; FDCAN2 AUTO/MANUAL LISTEN; LCD 50%%\n");
    int ret=display_init();
    if (ret<0) { printk("Display init failed: %d\n",ret); return ret; }
    ret=analyzer_start();
    if (ret<0) { printk("FDCAN2 init failed: %d (shown on screen)\n",ret); }
    ret=buttons_start();
    if (ret<0) { printk("Button init failed: %d\n",ret); }
    storage_start();
    printk("STORAGE: B record/stop; B long load. USB shell: capture info / capture dump <batch>.\n");
    analyzer_snapshot(&live, &state.rate_live);
    struct canvas *next = &canvases[0], *previous = NULL;
    struct key_event event;
    bool key_pending = false;
    for (;;) {
        if (key_pending) { key_action(&event); }
        while (k_msgq_get(&key_queue,&event,K_NO_WAIT)==0) { key_action(&event); }
        analyzer_snapshot(&live, &state.rate_live);
        storage_status_get(&state.storage);
        const struct analyzer_model *view=state.holding?&held:&live;
        ui_clamp(&state,view);
        ui_render(next,view,&state,k_uptime_get_32());
        struct panel_rect regions[DIRTY_MAX_REGIONS];
        size_t count = dirty_regions(next->pixels, previous ? previous->pixels : NULL, regions);
        for (size_t i = 0; i < count; ++i) {
            ret = display_update(next->pixels, &regions[i]);
            if (ret < 0) { printk("Display transfer failed: %d\n", ret); return ret; }
        }
        if (count != 0U) {
            previous = next;
            next = previous == &canvases[0] ? &canvases[1] : &canvases[0];
        }
        key_pending = k_msgq_get(&key_queue, &event, K_MSEC(100)) == 0;
    }
}
