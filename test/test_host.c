/* Host tests execute the same model, button FSM and pixel renderer as firmware. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "model.h"
#include "button.h"
#include "gfx.h"
#include "ui.h"
#include "dirty.h"

static struct analyzer_model m;
static struct canvas c;
static struct canvas other;

static size_t check_regions(const uint8_t *old, const uint8_t *new)
{
    struct panel_rect regions[DIRTY_MAX_REGIONS];
    size_t n = dirty_regions(new, old, regions);
    assert(n <= DIRTY_MAX_REGIONS);
    size_t sent = 0;
    for (size_t i = 0; i < n; ++i) {
        struct panel_rect r = regions[i];
        if (!panel_window_valid(&r)) {
            fprintf(stderr, "Invalid panel window: x=%u..%u, y=%u..%u, width=%u\n",
                    r.x0, r.x1, r.y0, r.y1, r.x1 - r.x0 + 1U);
            assert(panel_window_valid(&r));
        }
        sent += (r.x1 - r.x0 + 1U) * (r.y1 - r.y0 + 1U) * 2U;
    }
    for (unsigned int y = 0; y < PANEL_HEIGHT; ++y) {
        for (unsigned int x = 0; x < PANEL_WIDTH; ++x) {
            size_t offset = (y * PANEL_WIDTH + x) * 2U;
            if (old && memcmp(old + offset, new + offset, 2) == 0) { continue; }
            bool covered = false;
            for (size_t i = 0; i < n; ++i) {
                struct panel_rect r = regions[i];
                if (r.x0 <= x && x <= r.x1 && r.y0 <= y && y <= r.y1) { covered = true; }
            }
            assert(covered);
        }
    }
    return sent;
}

static void test_dirty(void)
{
    model_init(&m, 0);
    m.bus = BUS_ACTIVE;
    struct ui_state state = {0};
    ui_render(&c, &m, &state, 0);
    assert(check_regions(NULL, c.pixels) == GFX_BYTES);
    assert(check_regions(c.pixels, c.pixels) == 0);
    m.rate = 386;
    m.total = 12345;
    ui_render(&other, &m, &state, 1000);
    size_t sent = check_regions(c.pixels, other.pixels);
    assert(sent > 0 && sent < GFX_BYTES / 3U);
    printf("Overview counter change: %zu/%u pixel bytes\n", sent, GFX_BYTES);
    c = other;

    for (enum ui_page page = PAGE_FRAMES; page < PAGE_COUNT; ++page) {
        state.page = page;
        ui_render(&other, &m, &state, 1000);
        assert(check_regions(c.pixels, other.pixels) > 0);
        c = other;
    }
    state.page = PAGE_OVERVIEW;
    ui_render(&other, &m, &state, 1000);
    assert(check_regions(c.pixels, other.pixels) > 0);
    gfx_clear(&c, C_NAVY);
    other = c;
    gfx_pixel(&other, 0, 0, C_RED);
    gfx_pixel(&other, 239, 119, C_IVORY);
    assert(check_regions(c.pixels, other.pixels) == 32); /* Two 4x2 windows. */
}

static void test_model(void)
{
    model_init(&m,0);
    struct rx_event event={ .id=0x123, .flags=FRAME_FD|FRAME_BRS, .length=64, .at_ms=25 };
    for (unsigned int i=0;i<64U;++i) { event.data[i]=(uint8_t)i; }
    assert(model_receive(&m,&event));
    assert(m.total==1 && m.fd_total==1 && m.id_count==1);
    assert(memcmp(m.ids[0].data,event.data,64)==0);
    event.at_ms=100;
    assert(model_receive(&m,&event));
    assert(m.ids[0].count==2 && m.ids[0].seen_ms==100);
    event.flags=FRAME_EXT;
    event.length=8;
    assert(model_receive(&m,&event));
    assert(m.id_count==2 && m.classic_total==1); /* S 123 != X 00000123 */
    event.id=0x456; event.flags=FRAME_RTR;
    assert(model_receive(&m,&event));
    assert(m.ids[2].length==8 && m.ids[2].data[7]==0);
    model_tick(&m,1000);
    assert(m.rate==4);
    model_tick(&m,2000);
    assert(m.rate==0);
    event.length=65;
    assert(!model_receive(&m,&event));
    assert(m.total==4);
    event.length=9; event.flags=0;
    assert(!model_receive(&m,&event)); /* classic payload cannot exceed 8 */
    event.length=8; event.id=0x800;
    assert(!model_receive(&m,&event)); /* exceeds 11-bit identifier */
    event.flags=FRAME_EXT; event.id=0x20000000;
    assert(!model_receive(&m,&event)); /* exceeds 29-bit identifier */
    event.id=1; event.flags=FRAME_FD|FRAME_RTR;
    assert(!model_receive(&m,&event)); /* FD has no remote frames */
    model_init(&m,0);
    event.flags=0; event.length=8;
    for (unsigned int i=0;i<ANALYZER_IDS+2U;++i) {
        event.id=i; assert(model_receive(&m,&event));
    }
    assert(m.total==66 && m.id_count==64 && m.cache_limited);
    event.id=0; assert(model_receive(&m,&event));
    assert(m.ids[0].count==2);
    m.bus=BUS_WARNING; m.rx_errors=97; m.queue_drops=8;
    model_clear(&m,200);
    assert(m.total==0 && m.id_count==0 && !m.cache_limited && m.queue_drops==0);
    assert(m.bus==BUS_WARNING && m.rx_errors==97); /* hardware health is not cleared */
    model_init(&m,UINT32_MAX-500U);
    event.id=1; event.at_ms=UINT32_MAX-100U;
    assert(model_receive(&m,&event));
    model_tick(&m,499);
    assert(m.rate==1); /* uptime wrap: elapsed is 1000 ms */
    m.total=m.ids[0].count=UINT32_MAX;
    assert(model_receive(&m,&event));
    assert(m.total==UINT32_MAX && m.ids[0].count==UINT32_MAX);
    model_tick(&m,1499);
    assert(m.rate==1); /* rate still works when cumulative counters saturate */
}

static void test_buttons(void)
{
    struct button_state b;
    button_init(&b,false,0);
    assert(button_step(&b,true,10)==PRESS_NONE);
    assert(button_step(&b,false,20)==PRESS_NONE); /* bounce */
    assert(button_step(&b,true,30)==PRESS_NONE);
    assert(button_step(&b,true,55)==PRESS_NONE);
    assert(button_step(&b,false,200)==PRESS_NONE);
    assert(button_step(&b,false,225)==PRESS_SHORT);
    assert(button_step(&b,false,230)==PRESS_NONE);
    assert(button_step(&b,true,300)==PRESS_NONE);
    assert(button_step(&b,true,325)==PRESS_NONE);
    assert(button_step(&b,true,1024)==PRESS_NONE);
    assert(button_step(&b,true,1025)==PRESS_LONG);
    assert(button_step(&b,true,1500)==PRESS_NONE);
    assert(button_step(&b,false,1501)==PRESS_NONE);
    assert(button_step(&b,false,1526)==PRESS_NONE); /* no short after long */
    button_init(&b,true,0);
    assert(button_step(&b,true,1000)==PRESS_NONE); /* held during startup */
    assert(button_step(&b,false,1001)==PRESS_NONE);
    assert(button_step(&b,false,1026)==PRESS_NONE);
    button_init(&b,false,UINT32_MAX-100U);
    assert(button_step(&b,true,UINT32_MAX-50U)==PRESS_NONE);
    assert(button_step(&b,true,UINT32_MAX-25U)==PRESS_NONE);
    assert(button_step(&b,true,674U)==PRESS_LONG);
}

static void test_navigation(void)
{
    model_init(&m,0);
    struct rx_event e={ .id=1, .length=64, .flags=FRAME_FD };
    assert(model_receive(&m,&e));
    e.id=2; assert(model_receive(&m,&e));
    struct ui_state s={0};
    assert(ui_button(&s,&m,1,PRESS_SHORT,0)==UI_CAPTURE_HOLD && s.holding);
    assert(ui_button(&s,&m,0,PRESS_SHORT,0)==UI_NO_ACTION && s.page==PAGE_FRAMES);
    (void)ui_button(&s,&m,1,PRESS_SHORT,0);
    assert(s.selected==1);
    (void)ui_button(&s,&m,0,PRESS_SHORT,0);
    assert(s.page==PAGE_DATA && s.byte_page==0);
    for (int i=0;i<4;++i) { (void)ui_button(&s,&m,1,PRESS_SHORT,0); }
    assert(s.byte_page==0);
    s.byte_page=3; m.ids[1].length=8;
    ui_clamp(&s,&m); assert(s.byte_page==0);
    assert(ui_button(&s,&m,1,PRESS_LONG,0)==UI_CAPTURE_HOLD && !s.holding);
    assert(ui_button(&s,&m,0,PRESS_SHORT,0)==UI_NO_ACTION && s.page==PAGE_STORAGE);
    assert(ui_button(&s,&m,1,PRESS_SHORT,0)==UI_TOGGLE_RECORD);
    assert(ui_button(&s,&m,1,PRESS_LONG,0)==UI_RESTORE);
    assert(ui_button(&s,&m,0,PRESS_SHORT,0)==UI_NO_ACTION && s.page==PAGE_RATE);
    assert(ui_button(&s,&m,1,PRESS_SHORT,0)==UI_NO_ACTION && s.rate_selection==1);
    assert(ui_button(&s,&m,1,PRESS_LONG,0)==UI_APPLY_RATE);
    assert(ui_button(&s,&m,0,PRESS_SHORT,0)==UI_NO_ACTION && s.page==PAGE_OVERVIEW);
    assert(ui_button(&s,&m,0,PRESS_LONG,100)==UI_CLEAR_STATS);
    assert(s.selected==0 && s.cleared_notice);
    m.id_count=0; ui_clamp(&s,&m);
    (void)ui_button(&s,&m,1,PRESS_SHORT,0);
    assert(s.selected==0 && s.byte_page==0);
}

static void save(const char *name,const struct analyzer_model *model,struct ui_state state)
{
    ui_clamp(&state,model);
    ui_render(&c,model,&state,model->now_ms);
    if (c.text_overflow) { fprintf(stderr,"Text overflow: %s\n",name); }
    assert(!c.text_overflow);
    char path[160]; snprintf(path,sizeof(path),"docs/%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n240 120\n255\n");
    for (int y=0;y<120;++y) {
        for (int x=0;x<240;++x) {
            uint16_t p=gfx_read(&c,x,y);
            uint8_t rgb[3]={ (uint8_t)(((p>>11)&31)*255/31),
                             (uint8_t)(((p>>5)&63)*255/63),
                             (uint8_t)((p&31)*255/31) };
            assert(fwrite(rgb,1,3,f)==3);
        }
    }
    fclose(f);
}
static void test_text_edges(void)
{
    for (int size = 1; size <= 3; ++size) {
        gfx_clear(&c, C_NAVY);
        gfx_text(&c, 4, 4, "08", size, C_IVORY);
        bool ink = false;
        bool edge = false;
        for (int y = 0; y < 30; ++y) {
            for (int x = 0; x < 48; ++x) {
                uint16_t pixel = gfx_read(&c, x, y);
                ink |= pixel == C_IVORY;
                edge |= pixel != C_NAVY && pixel != C_IVORY;
            }
        }
        /* The panel must show only the selected text and background colors. */
        assert(ink && !edge && !c.text_overflow);
    }
}

static void test_render(void)
{
    gfx_clear(&c,C_NAVY);
    gfx_pixel(&c,0,0,C_YELLOW);
    gfx_pixel(&c,239,119,C_RED);
    assert(c.pixels[239U*120U*2U]==(C_YELLOW>>8));
    assert(c.pixels[119U*2U]==(C_RED>>8));
    uint8_t first=c.pixels[0];
    gfx_pixel(&c,-1,-1,C_RED);
    assert(c.pixels[0]==first);
    model_init(&m,0); m.bus=BUS_ACTIVE;
    struct rx_event e={ .id=0x123,.length=8,.at_ms=12 };
    for (unsigned int i=0;i<64U;++i) { e.data[i]=(uint8_t)(i*7U); }
    assert(model_receive(&m,&e));
    e.id=0x18ff50e5; e.flags=FRAME_EXT|FRAME_FD|FRAME_BRS; e.length=64;
    assert(model_receive(&m,&e));
    e.id=0x321; e.flags=FRAME_FD; e.length=16;
    assert(model_receive(&m,&e));
    m.total=12842; m.rate=386; m.now_ms=2040; m.ids[1].seen_ms=2028;
    struct ui_state s={0};
    save("overview",&m,s);
    s.page=PAGE_FRAMES; s.selected=1; save("frames",&m,s);
    s.page=PAGE_DATA; save("data",&m,s);
    s.byte_page=3; save("data_page4",&m,s);
    s.holding=true; save("held",&m,s);
    m.ids[1].flags=FRAME_EXT|FRAME_RTR; m.ids[1].length=8;
    s.holding=false; s.byte_page=0; save("remote",&m,s);
    model_init(&m,0); m.bus=BUS_ACTIVE;
    s.page=PAGE_FRAMES; save("empty",&m,s);
    s.page=PAGE_OVERVIEW; m.bus=BUS_INIT_ERROR; m.init_error=-134;
    save("error",&m,s);
    /* Worst-case counters and identifier widths must not clip at screen edges. */
    m.init_error=0; m.bus=BUS_PASSIVE; m.id_count=64; m.cache_limited=true;
    m.total=m.rate=m.queue_drops=m.overruns=UINT32_MAX;
    m.rx_errors=m.tx_errors=255; m.now_ms=UINT32_MAX;
    m.ids[63]=(struct id_record){ .id=0x1fffffff,.flags=FRAME_EXT|FRAME_FD|FRAME_BRS,
                               .length=64,.count=UINT32_MAX,.seen_ms=0 };
    save("worst_overview",&m,s);
    s.page=PAGE_FRAMES; s.selected=63; save("worst_frames",&m,s);
    s.page=PAGE_DATA; s.byte_page=3; save("worst_data",&m,s);
    s.cleared_notice=true; s.notice_until_ms=UINT32_MAX+100U;
    save("cleared",&m,s);
    s = (struct ui_state){ .page = PAGE_STORAGE };
    s.storage = (struct storage_status){ .ready = true, .recording = true,
                                         .batches = 2044U, .snapshots = 2U };
    save("storage",&m,s);
    s.storage.dropped = UINT32_MAX;
    s.storage.snapshots = UINT32_MAX;
    s.storage.recording = false;
    s.storage.error = -2147483647;
    s.storage.snapshot_result = -1;
    save("storage_error",&m,s);
}
static void test_rate_search(void)
{
    struct rate_search search;
    uint32_t now = 0;
    rate_search_reset(&search, now);
    assert(search.candidate.nominal == 500 && search.candidate.data == 2000);

    /* Silent bus must exhaust the list, never fabricate a lock. */
    unsigned int tried = 0;
    enum scan_result r;
    do {
        assert(++tried < 70);
        assert(search.candidate.nominal <= search.candidate.data);
        now += 300;
        r = rate_search_poll(&search, now, false);
        assert(r == SCAN_NEXT || r == SCAN_EXHAUSTED);
        rate_search_trial(&search, now);
    } while (r == SCAN_NEXT);
    assert(tried >= 50);

    /* 250K Classic proves nominal. A full data sweep still proves no BRS rate. */
    rate_search_reset(&search, 0);
    assert(rate_search_poll(&search, 300, false) == SCAN_NEXT);
    assert(search.candidate.nominal == 250);
    now = 300;
    for (unsigned int j = 0; j < 7; ++j) {
        rate_search_trial(&search, now);
        assert(search.candidate.nominal == 250);
        for (unsigned int i = 0; i < 10; ++i) { rate_search_frame(&search, false); }
        now += 300;
        r = rate_search_poll(&search, now, false);
        assert(r == (j < 6 ? SCAN_NEXT : SCAN_LOCK));
    }
    assert(search.best.nominal == 250 && search.best.data == 2000 && search.best_brs == 0);

    /* FD+BRS-only traffic requires reaching the pair even without nominal evidence. */
    rate_search_reset(&search, 0);
    now = 0;
    for (unsigned int i = 0; i < 9; ++i) {
        now += 300;
        assert(rate_search_poll(&search, now, false) == SCAN_NEXT);
        rate_search_trial(&search, now);
    }
    assert(search.candidate.nominal == 500 && search.candidate.data == 4000);
    rate_search_frame(&search, true);
    assert(rate_search_poll(&search, now + 1000, false) == SCAN_PENDING);
    for (unsigned int i = 1; i < 10; ++i) { rate_search_frame(&search, true); }
    assert(rate_search_poll(&search, now + 9000, false) == SCAN_LOCK);
    assert(search.best.nominal == 500 && search.best.data == 4000 && search.best_brs == 10);

    /* With a mix, a valid later BRS profile supersedes the Classic-only anchor. */
    rate_search_reset(&search, UINT32_MAX - 150U);
    for (unsigned int i = 0; i < 10; ++i) { rate_search_frame(&search, false); }
    assert(rate_search_poll(&search, 149, false) == SCAN_NEXT);
    assert(search.anchored && search.candidate.nominal == 500 && search.candidate.data == 4000);
    rate_search_trial(&search, 149);
    for (unsigned int i = 0; i < 10; ++i) { rate_search_frame(&search, true); }
    assert(rate_search_poll(&search, 200, false) == SCAN_LOCK);
    assert(search.best_brs == 10 && search.best.data == 4000);

    rate_search_reset(&search, 0);
    assert(rate_search_poll(&search, 0, true) == SCAN_NEXT);
    assert(search.candidate.nominal == 250);
}


static void render_v5_previews(void)
{
    model_init(&m, 0);
    struct ui_state state = {
        .page = PAGE_RATE,
        .rate_live = { .active = {500, 2000}, .phase = RATE_SCAN },
    };
    save("preview_scan", &m, state);

    struct rx_event frame = { .id = 0x123, .length = 8, .at_ms = 2000 };
    for (unsigned int i = 0; i < 64; ++i) { frame.data[i] = (uint8_t)(i * 7U); }
    assert(model_receive(&m, &frame));
    m.bus = BUS_ACTIVE;
    m.now_ms = 2040;
    m.total = 48;
    m.rate = 12;
    m.ids[0].count = 48;
    state.page = PAGE_OVERVIEW;
    state.rate_live = (struct rate_view){
        .active = {250, 2000}, .phase = RATE_LOCKED,
        .nominal_ok = true, .saw_frame = true, .last_frame_ms = 2000,
    };
    save("preview_classic", &m, state);

    frame.id = 0x18ff50e5;
    frame.flags = FRAME_EXT | FRAME_FD | FRAME_BRS;
    frame.length = 64;
    assert(model_receive(&m, &frame));
    frame.id = 0x321;
    frame.flags = FRAME_FD;
    frame.length = 16;
    assert(model_receive(&m, &frame));
    m.total = 12842;
    m.rate = 386;
    m.ids[1].count = 12400;
    m.ids[1].seen_ms = 2028;
    state.rate_live = (struct rate_view){
        .active = {500, 4000}, .phase = RATE_LOCKED, .nominal_ok = true,
        .data_ok = true, .saw_frame = true, .last_frame_ms = 2028,
    };
    save("preview_fd", &m, state);

    state.page = PAGE_RATE;
    state.rate_live.active.data = 2000;
    save("preview_rate_locked", &m, state);

    state.rate_live.active.data = 4000;
    state.rate_live.phase = RATE_MANUAL;
    state.rate_selection = 5;
    save("preview_manual", &m, state);

    state.rate_live.phase = RATE_LOCKED;
    state.page = PAGE_FRAMES;
    state.selected = 1;
    save("preview_ids", &m, state);
    state.page = PAGE_DATA;
    save("preview_data", &m, state);
    state.page = PAGE_STORAGE;
    state.storage = (struct storage_status){
        .ready = true, .recording = true, .batches = 18, .snapshots = 2, .snapshot_result = 1,
    };
    save("preview_storage", &m, state);

    state.page = PAGE_OVERVIEW;
    state.storage.recording = false;
    m.now_ms = 6000;
    m.rate = 0;
    save("preview_idle", &m, state);
}
int main(void)
{
    test_model(); test_buttons(); test_navigation(); test_text_edges(); test_render();
    test_dirty(); test_rate_search(); render_v5_previews();
    puts("PASS: model, buttons, navigation, render, dirty regions and bitrate scans");
    return 0;
}
