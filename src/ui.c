#include "ui.h"
#include <stdio.h>

static unsigned int data_pages(const struct id_record *r)
{
    return (r->flags & FRAME_RTR) || r->length == 0U ? 1U : (r->length + 15U) / 16U;
}
void ui_clamp(struct ui_state *s, const struct analyzer_model *m)
{
    if (m->id_count == 0U) { s->selected = s->byte_page = 0; return; }
    if (s->selected >= m->id_count) { s->selected = 0; }
    if (s->byte_page >= data_pages(&m->ids[s->selected])) { s->byte_page = 0; }
}
enum ui_action ui_button(struct ui_state *s, const struct analyzer_model *m,
                         unsigned int key, enum press_event press, uint32_t now)
{
    ui_clamp(s,m);
    if (press == PRESS_NONE) { return UI_NO_ACTION; }
    if (key == 0U && press == PRESS_LONG) {
        s->selected = s->byte_page = 0;
        s->cleared_notice = true;
        s->notice_until_ms = now + 1200U;
        return UI_CLEAR_STATS;
    }
    if (key == 0U) {
        s->page = (enum ui_page)((s->page + 1U) % PAGE_COUNT);
        s->byte_page = 0;
    } else if (s->page == PAGE_RATE) {
        if (press == PRESS_LONG) { return UI_APPLY_RATE; }
        s->rate_selection = (s->rate_selection + 1U) % rate_preset_count();
    } else if (s->page == PAGE_STORAGE) {
        return press == PRESS_LONG ? UI_RESTORE : UI_TOGGLE_RECORD;
    } else if (press == PRESS_LONG || s->page == PAGE_OVERVIEW) {
        s->holding = !s->holding;
        return UI_CAPTURE_HOLD;
    } else if (s->page == PAGE_FRAMES && m->id_count != 0U) {
        s->selected = (s->selected + 1U) % m->id_count;
        s->byte_page = 0;
    } else if (s->page == PAGE_DATA && m->id_count != 0U) {
        s->byte_page = (s->byte_page + 1U) % data_pages(&m->ids[s->selected]);
    }
    return UI_NO_ACTION;
}

static const char *type_name(const struct id_record *r)
{
    if (r->flags & FRAME_RTR) { return "RTR"; }
    if (r->flags & FRAME_FD) { return (r->flags & FRAME_BRS) ? "FD+B" : "FD"; }
    return "CAN";
}
static const char *bus_name(enum bus_state bus)
{
    static const char *const names[] = { "ACTV", "WARN", "PASS", "OFF", "STOP", "INIT" };
    return bus <= BUS_INIT_ERROR ? names[bus] : "ERR";
}
/* Compact loss counters, rounded down to one fractional digit. */
static void compact(char *s, size_t n, uint32_t value)
{
    uint32_t divisor = value >= 1000000000U ? 1000000000U :
                       value >= 1000000U ? 1000000U : value >= 1000U ? 1000U : 1U;
    char unit = divisor == 1000000000U ? 'G' : divisor == 1000000U ? 'M' : 'K';
    if (divisor == 1U) { snprintf(s,n,"%u",(unsigned int)value); }
    else { snprintf(s,n,"%u.%c%c",(unsigned int)(value/divisor),
                    (int)('0'+(value%divisor)/(divisor/10U)),unit); }
}
static void header(struct canvas *c, const struct analyzer_model *m,
                   const struct ui_state *s, const char *title, const char *index, uint32_t wall)
{
    gfx_rect(c,0,0,240,20,C_YELLOW);
    gfx_text(c,6,3,title,2,C_NAVY);
    bool notice = s->cleared_notice && (int32_t)(wall-s->notice_until_ms) < 0;
    const char *sub = notice ? "CLEARED" : index;
    /* Keep the page indicator right-aligned beside the fixed status badge. */
    gfx_text(c,182-gfx_width(sub,1),7,sub,1,C_NAVY);
    bool fault = m->init_error != 0 || m->bus == BUS_OFF;
    uint16_t badge = s->holding || fault || s->storage.recording ? C_RED : C_NAVY;
    const char *state = s->holding ? "HOLD" : fault ? "ERR" :
                        s->storage.recording ? "REC" : "LIVE";
    gfx_rect(c,188,4,46,12,badge);
    gfx_text(c,188+(46-gfx_width(state,1))/2,7,state,1,C_IVORY);
}
static void footer(struct canvas *c, const struct ui_state *s)
{
    gfx_rect(c,0,107,240,1,C_YELLOW);
    gfx_text(c,6,111,"A PAGE",1,C_IVORY);
    const char *b = s->page == PAGE_OVERVIEW ? "B HOLD" :
                    s->page == PAGE_FRAMES ? "B NEXT" :
                    s->page == PAGE_DATA ? "B BYTES" :
                    s->page == PAGE_RATE ? "B NEXT / HOLD SET" : "B REC / HOLD LOAD";
    gfx_text(c,124,111,b,1,C_YELLOW);
}
static void empty(struct canvas *c)
{
    const char *message = "NO FRAMES";
    const char *mode = "LISTEN ONLY";
    gfx_text(c,(UI_WIDTH-gfx_width(message,2))/2,51,message,2,C_IVORY);
    gfx_text(c,(UI_WIDTH-gfx_width(mode,1))/2,78,mode,1,C_YELLOW);
}

static void rate_text(char *text, size_t size, unsigned int kbit)
{
    if (kbit == 0U) { snprintf(text, size, "--"); }
    else if (kbit % 1000U == 0U) { snprintf(text, size, "%uM", kbit / 1000U); }
    else { snprintf(text, size, "%uK", kbit); }
}

static const char *rate_label(const struct rate_view *v, uint32_t now)
{
    static const char *const labels[] = {
        "SCANNING", "DATA SCAN", "NO VALID", "LOCKED", "MANUAL", "RATE ERR", "SNAP",
    };
    if (v->phase == RATE_LOCKED && v->saw_frame &&
        now - v->last_frame_ms >= RATE_IDLE_MS) {
        return "IDLE";
    }
    return labels[v->phase];
}

static void overview(struct canvas *c, const struct analyzer_model *m,
                     const struct ui_state *s)
{
    char text[40], a[16], b[16];
    const struct rate_view *rate = s->holding ? &s->rate_held : &s->rate_live;
    if (m->init_error != 0) {
        snprintf(text,sizeof(text),"CAN ERR %d",m->init_error);
        gfx_text(c,6,27,text,1,C_RED);
    } else {
        bool show_nominal = rate->nominal_ok || rate->phase == RATE_MANUAL;
        rate_text(a, sizeof(a), show_nominal ? rate->active.nominal : 0);
        if (rate->data_ok) {
            rate_text(b, sizeof(b), rate->active.data);
        } else {
            snprintf(b, sizeof(b), "%s", rate->phase == RATE_SCAN_DATA ? "?" : "N/A");
        }
        snprintf(text, sizeof(text), "NOM %.7s DATA %.7s", a, b);
        gfx_text(c,6,27,text,1,C_IVORY);
    }
    gfx_text(c,120,27,"LISTEN",1,C_YELLOW);
    gfx_text(c,174,27,rate_label(rate,m->now_ms),1,
             rate->phase == RATE_ERROR ? C_RED : C_YELLOW);
    gfx_text(c,6,43,"RX/S",1,C_IVORY);
    gfx_text(c,152,43,"IDS",1,C_IVORY);
    if (m->rate > 99999U) { snprintf(text,sizeof(text),"99999+"); }
    else { snprintf(text,sizeof(text),"%u",(unsigned int)m->rate); }
    gfx_text_mono(c,6,55,text,3,C_YELLOW);
    snprintf(text,sizeof(text),"%u%s",m->id_count,m->cache_limited?"+":"");
    gfx_text_mono(c,152,55,text,3,C_IVORY);
    snprintf(text,sizeof(text),"RX %u",(unsigned int)m->total);
    gfx_text(c,6,86,text,1,C_IVORY);
    snprintf(text,sizeof(text),"BUS %s",bus_name(m->bus));
    gfx_text(c,140,86,text,1,m->bus==BUS_ACTIVE?C_IVORY:C_RED);
    compact(a,sizeof(a),m->queue_drops);
    compact(b,sizeof(b),m->overruns);
    snprintf(text,sizeof(text),"LOSS %s/%s",a,b);
    gfx_text(c,6,97,text,1,m->queue_drops||m->overruns?C_RED:C_IVORY);
    snprintf(text,sizeof(text),"ERR %u/%u",m->rx_errors,m->tx_errors);
    gfx_text(c,140,97,text,1,m->rx_errors||m->tx_errors?C_RED:C_IVORY);
}
static void frames(struct canvas *c, const struct analyzer_model *m, const struct ui_state *s)
{
    gfx_text(c,8,26,"ID",1,C_IVORY);
    gfx_text(c,146,26,"LEN",1,C_IVORY);
    gfx_text(c,188,26,"TYPE",1,C_IVORY);
    if (m->id_count == 0U) { empty(c); return; }
    unsigned int first = s->selected/3U*3U;
    for (unsigned int row=0; row<3U && first+row<m->id_count; ++row) {
        unsigned int i=first+row;
        const struct id_record *r=&m->ids[i];
        int y=37+(int)row*23;
        uint16_t fg=i==s->selected?C_NAVY:C_IVORY;
        if (i==s->selected) { gfx_rect(c,4,y-1,232,20,C_IVORY); }
        char text[24];
        snprintf(text,sizeof(text),r->flags&FRAME_EXT?"X %08X":"S %03X",(unsigned int)r->id);
        gfx_text_mono(c,8,y+2,text,2,fg);
        snprintf(text,sizeof(text),"%u",r->length);
        gfx_text_mono(c,146,y+2,text,2,fg);
        gfx_text(c,188,y+6,type_name(r),1,fg);
    }
}
static void data(struct canvas *c, const struct analyzer_model *m, const struct ui_state *s)
{
    if (m->id_count==0U) { empty(c); return; }
    const struct id_record *r=&m->ids[s->selected];
    char text[40];
    snprintf(text,sizeof(text),r->flags&FRAME_EXT?"X %08X":"S %03X",(unsigned int)r->id);
    gfx_text_mono(c,6,25,text,2,C_YELLOW);
    snprintf(text,sizeof(text),"%s %uB",type_name(r),r->length);
    gfx_text(c,146,29,text,1,C_IVORY);
    if (r->flags&FRAME_RTR) {
        gfx_text(c,12,53,"REMOTE FRAME",2,C_IVORY);
        gfx_text(c,12,77,"NO PAYLOAD",1,C_YELLOW);
    } else {
        /* Show 16 bytes per page in four fixed columns. Short frames use dashes
         * in unused cells so the byte offsets keep their positions. */
        unsigned int start=s->byte_page*16U;
        for (unsigned int row=0; row<4U; ++row) {
            int y=44+(int)row*16;
            unsigned int offset=start+row*4U;
            snprintf(text,sizeof(text),"%02X",offset);
            gfx_text_mono(c,6,y+4,text,1,C_YELLOW);
            for (unsigned int col=0; col<4U; ++col) {
                unsigned int i=offset+col;
                if (i<r->length) { snprintf(text,sizeof(text),"%02X",r->data[i]); }
                else { snprintf(text,sizeof(text),"--"); }
                gfx_text_mono(c,30+(int)col*36,y,text,2,C_IVORY);
            }
        }
    }
    gfx_text(c,172,49,"COUNT",1,C_YELLOW);
    snprintf(text,sizeof(text),"%u",(unsigned int)r->count);
    gfx_text(c,172,62,text,1,C_IVORY);
    gfx_text(c,172,81,"AGE",1,C_YELLOW);
    uint32_t age=m->now_ms-r->seen_ms;
    snprintf(text,sizeof(text),age<1000U?"%uMS":"%uS",(unsigned int)(age<1000U?age:age/1000U));
    gfx_text(c,172,94,text,1,C_IVORY);
}
static void storage_page(struct canvas *c, const struct ui_state *s)
{
    const struct storage_status *st = &s->storage;
    const char *mode = st->error != 0 ? "FLASH ERR" : !st->ready ? "SCAN" :
                       st->recording ? "RECORDING" : st->flushing ? "FLUSHING" : "READY";
    gfx_text(c, 6, 29, mode, 2, st->error != 0 ? C_RED : C_YELLOW);
    char text[32];
    snprintf(text, sizeof(text), "BATCH %u", (unsigned int)st->batches);
    gfx_text(c, 6, 55, text, 1, C_IVORY);
    snprintf(text, sizeof(text), "DROP %u", (unsigned int)st->dropped);
    gfx_text(c, 124, 55, text, 1, st->dropped ? C_RED : C_IVORY);
    if (st->snapshots != 0U) {
        snprintf(text, sizeof(text), "SNAP #%u", (unsigned int)st->snapshots);
    } else {
        snprintf(text, sizeof(text), "NO SNAPSHOT");
    }
    gfx_text(c, 6, 75, text, 1, C_IVORY);
    if (st->snapshot_result < 0) {
        gfx_text(c, 124, 75, "SAVE ERR", 1, C_RED);
    } else if (st->snapshot_result > 0) {
        gfx_text(c, 124, 75, "SAVED", 1, C_YELLOW);
    }
    if (st->error != 0) {
        snprintf(text, sizeof(text), "NOR ERR %d", st->error);
        gfx_text(c, 6, 93, text, 1, C_RED);
    }
}

static void draw_rate_page(struct canvas *c, const struct ui_state *s, uint32_t now)
{
    char n[12], d[12], text[32];
    const struct rate_view *v = &s->rate_live;
    gfx_text(c, 6, 26, rate_label(v, now), 2, v->phase == RATE_ERROR ? C_RED : C_YELLOW);
    gfx_text(c, 164, 30, "LISTEN ONLY", 1, C_IVORY);
    rate_text(n, sizeof(n), v->active.nominal);
    rate_text(d, sizeof(d), v->active.data);
    snprintf(text, sizeof(text), "%s %s / %s",
             v->phase == RATE_SCAN || v->phase == RATE_SCAN_DATA ? "TRY" : "SET", n, d);
    gfx_text(c, 6, 51, text, 1, C_IVORY);

    struct rate_pair choice = rate_preset(s->rate_selection);
    gfx_text(c, 6, 77, "SELECT", 1, C_IVORY);
    if (choice.nominal != 0U) {
        rate_text(n, sizeof(n), choice.nominal);
        rate_text(d, sizeof(d), choice.data);
        snprintf(text, sizeof(text), "%s / %s", n, d);
    } else {
        snprintf(text, sizeof(text), "AUTO");
    }
    gfx_text(c, 62, 73, text, 2, C_YELLOW);
}

void ui_render(struct canvas *c, const struct analyzer_model *m,
               const struct ui_state *s, uint32_t wall_ms)
{
    char index[20];
    gfx_clear(c,C_NAVY);
    if (s->page==PAGE_OVERVIEW) {
        header(c,m,s,"Overview","1/5",wall_ms);
        overview(c,m,s);
    } else if (s->page==PAGE_FRAMES) {
        snprintf(index,sizeof(index),"%02u/%02u",m->id_count?s->selected+1U:0U,m->id_count);
        header(c,m,s,"Frames",index,wall_ms);
        frames(c,m,s);
    } else if (s->page == PAGE_DATA) {
        unsigned int n=m->id_count?data_pages(&m->ids[s->selected]):1U;
        snprintf(index,sizeof(index),"%u/%u",s->byte_page+1U,n);
        header(c,m,s,"Data",index,wall_ms);
        data(c,m,s);
    } else if (s->page == PAGE_STORAGE) {
        header(c,m,s,"Storage","4/5",wall_ms);
        storage_page(c,s);
    } else {
        header(c,m,s,"Rate","5/5",wall_ms);
        draw_rate_page(c,s,wall_ms);
    }
    footer(c,s);
    if (s->storage_note != NOTE_NONE && (int32_t)(wall_ms - s->storage_note_until) < 0) {
        static const char *const notes[] = { "", "RESTORED", "NO SNAPSHOT", "FLASH BUSY", "SAVE ERR" };
        const char *label = notes[s->storage_note];
        gfx_rect(c, 74, 23, 92, 15, s->storage_note == NOTE_RESTORED ? C_YELLOW : C_RED);
        gfx_text(c, 120 - gfx_width(label, 1) / 2, 27, label, 1,
                 s->storage_note == NOTE_RESTORED ? C_NAVY : C_IVORY);
    }
}
