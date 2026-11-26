#ifndef ANALYZER_UI_H
#define ANALYZER_UI_H
#include "gfx.h"
#include "model.h"
#include "button.h"
#include "storage.h"
enum ui_page { PAGE_OVERVIEW, PAGE_FRAMES, PAGE_DATA, PAGE_STORAGE, PAGE_RATE, PAGE_COUNT };
enum ui_action { UI_NO_ACTION, UI_CAPTURE_HOLD, UI_CLEAR_STATS, UI_TOGGLE_RECORD, UI_RESTORE,
                 UI_APPLY_RATE };
enum storage_note { NOTE_NONE, NOTE_RESTORED, NOTE_NO_SNAPSHOT, NOTE_BUSY, NOTE_SAVE_ERROR };
struct ui_state {
    enum ui_page page;
    unsigned int selected, byte_page;
    bool holding, cleared_notice;
    uint32_t notice_until_ms;
    size_t rate_selection;
    struct rate_view rate_live, rate_held;
    struct storage_status storage;
    enum storage_note storage_note;
    uint32_t storage_note_until;
};
void ui_clamp(struct ui_state *s, const struct analyzer_model *m);
enum ui_action ui_button(struct ui_state *s, const struct analyzer_model *m,
                         unsigned int key, enum press_event press, uint32_t now);
void ui_render(struct canvas *c, const struct analyzer_model *m,
               const struct ui_state *s, uint32_t wall_ms);
#endif
