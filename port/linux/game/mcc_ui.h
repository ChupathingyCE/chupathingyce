#ifndef MCC_UI_H
#define MCC_UI_H

struct widget_instance;
struct event_record;

/* TRUE means this MCC-owned callback supplied result. FALSE leaves the
 * established callback dispatcher entirely responsible for the widget. */
boolean mcc_ui_event_function(struct widget_instance *widget,
    struct event_record *event, word function, boolean *deleted, boolean *result);
boolean mcc_ui_settings_needed(char const *map_name);
/* Handles only the MCC-owned multiplayer New Game button. The caller must
 * finish dispatch immediately when handled, including a host-only denial.
 * A successful transition synchronously deletes the active widget tree. */
boolean mcc_ui_new_game(struct widget_instance *widget, boolean *deleted);

#endif
