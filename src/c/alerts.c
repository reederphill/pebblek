#include "alerts.h"

#include "protocol.h"
#include "state.h"
#include "ui.h"

typedef struct Alert {
    struct Alert* next;
    int id;
    AlertLevel level;
    AlertLevel peak;  // deduplicates retransmits and lower-severity oscillations
    struct Alert* next_notice;
    bool needs_dismissal;
    bool cleared;
    char message[111];
} Alert;

static Alert* s_active;  // highest severity first, then stable phone-assigned ID
// The notice queue retains an alert until Back, even after it leaves s_active.
static Alert* s_notice;
static Window* s_notice_window;
static ScrollLayer* s_notice_scroll;
static TextLayer* s_notice_text;
static Layer* s_notice_frame;
static bool s_synced;
static bool s_out_of_memory;
static Window* s_list;
static ScrollLayer* s_scroll;
static Layer* s_rows;
static Layer* s_header;

static const char* level_name(AlertLevel level) {
    static const char* names[] = {"", "ALERT", "WARNING", "ALARM", "EMERGENCY"};
    return names[level];
}

static GColor level_color(AlertLevel level) {
    switch (level) {
        case ALERT_EMERGENCY: return GColorRed;
        case ALERT_ALARM: return GColorOrange;
        case ALERT_WARNING: return GColorYellow;
        default: return GColorLightGray;
    }
}

static void text(GContext* ctx, const char* value, GFont font, GColor color,
                 GRect box, GTextAlignment alignment) {
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, value, font, box, GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

static bool connected(void) {
    return app_state()->conn == CONN_CONNECTED && connection_service_peek_pebble_app_connection();
}

static GRect notice_viewport(void) {
    GRect bounds = layer_get_bounds(window_get_root_layer(s_notice_window));
    const int inset = PBL_IF_ROUND_ELSE(28, 8);
    const int top = PBL_IF_ROUND_ELSE(66, 49);
    const int bottom = PBL_IF_ROUND_ELSE(66, 39);
    return GRect(inset, top, bounds.size.w - 2 * inset, bounds.size.h - top - bottom);
}

static void refresh_notice(bool scroll_to_top) {
    if (!s_notice || !s_notice_text) return;
    const GRect view = notice_viewport();
    const GFont font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    const int height = graphics_text_layout_get_content_size(s_notice->message, font,
        GRect(0, 0, view.size.w, 4096), GTextOverflowModeWordWrap, GTextAlignmentCenter).h + 5;
    const int y = height < view.size.h ? (view.size.h - height) / 2 : 0;
    layer_set_frame(text_layer_get_layer(s_notice_text), GRect(0, y, view.size.w, height));
    text_layer_set_text(s_notice_text, s_notice->message);
    layer_mark_dirty(text_layer_get_layer(s_notice_text));
    scroll_layer_set_content_size(s_notice_scroll,
        GSize(view.size.w, height > view.size.h ? height : view.size.h));
    if (scroll_to_top) scroll_layer_set_content_offset(s_notice_scroll, GPointZero, false);
    layer_mark_dirty(s_notice_frame);
}

static void draw_notice_frame(Layer* layer, GContext* ctx) {
    if (!s_notice) return;
    const GRect b = layer_get_bounds(layer);
    const int top = PBL_IF_ROUND_ELSE(22, 0);
    const int footer = b.size.h - PBL_IF_ROUND_ELSE(57, 34);
    const GColor accent = level_color(s_notice->level);
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_rect(ctx, GRect(0, top, b.size.w, 43), 0, GCornerNone);
    text(ctx, s_notice->cleared ? "CLEARED" : level_name(s_notice->level),
         fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD), GColorBlack,
         GRect(0, top + 2, b.size.w, 37), GTextAlignmentCenter);
    text(ctx, s_notice->next_notice ? "Back for next alert" : "Back to dismiss",
         fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GColorWhite,
         GRect(0, footer, b.size.w, 33), GTextAlignmentCenter);
}

static void release_notice(void) {
    Alert* alert = s_notice;
    s_notice = alert->next_notice;
    alert->next_notice = NULL;
    alert->needs_dismissal = false;
    if (!alert->id) free(alert);  // already removed from the active list
}

static void dismiss_notice(ClickRecognizerRef recognizer, void* context) {
    if (!s_notice) return;
    release_notice();
    if (s_notice) refresh_notice(true);
    else window_stack_pop(false);
}

static void notice_click_config(void* context) {
    window_single_click_subscribe(BUTTON_ID_BACK, dismiss_notice);
}

static void notice_load(Window* window) {
    Layer* root = window_get_root_layer(window);
    s_notice_frame = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_notice_frame, draw_notice_frame);
    layer_add_child(root, s_notice_frame);
    const GRect view = notice_viewport();
    s_notice_scroll = scroll_layer_create(view);
    scroll_layer_set_shadow_hidden(s_notice_scroll, true);
    layer_add_child(root, scroll_layer_get_layer(s_notice_scroll));
    s_notice_text = text_layer_create((GRect){.size = view.size});
    text_layer_set_font(s_notice_text, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_color(s_notice_text, GColorWhite);
    text_layer_set_background_color(s_notice_text, GColorClear);
    text_layer_set_text_alignment(s_notice_text, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_notice_text, GTextOverflowModeWordWrap);
    scroll_layer_add_child(s_notice_scroll, text_layer_get_layer(s_notice_text));
    scroll_layer_set_callbacks(s_notice_scroll, (ScrollLayerCallbacks){.click_config_provider = notice_click_config});
    scroll_layer_set_click_config_onto_window(s_notice_scroll, window);
    refresh_notice(true);
}

static void notice_unload(Window* window) {
    text_layer_destroy(s_notice_text);
    scroll_layer_destroy(s_notice_scroll);
    layer_destroy(s_notice_frame);
    s_notice_text = NULL;
}

static void announce(Alert* alert) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "alert haptic id=%d level=%d", alert->id, (int)alert->level);
    vibes_short_pulse();
    if (alert->needs_dismissal) return;
    alert->needs_dismissal = true;
    Alert** tail = &s_notice;
    while (*tail) tail = &(*tail)->next_notice;
    *tail = alert;
    if (s_notice == alert) window_stack_push(s_notice_window, false);
    else layer_mark_dirty(s_notice_frame);
}

// Drop the active list's ownership. A queued notice keeps its text until Back.
static void retire_alert(Alert* alert, bool cleared) {
    if (!alert->needs_dismissal) {
        free(alert);
        return;
    }
    alert->id = 0;
    alert->cleared = cleared;
    if (alert == s_notice) refresh_notice(false);
}

static GRect list_viewport(void) {
    GRect bounds = layer_get_bounds(window_get_root_layer(s_list));
    const int inset = PBL_IF_ROUND_ELSE(33, 8);
    const int top = PBL_IF_ROUND_ELSE(55, 37);
    return GRect(inset, top, bounds.size.w - 2 * inset,
                 bounds.size.h - top - PBL_IF_ROUND_ELSE(32, 5));
}

static const char* list_status(void) {
    if (!connected()) return "Connection lost";
    if (!s_synced) return "Syncing alerts…";
    if (s_out_of_memory) return "Alert list incomplete";
    return s_active ? NULL : "No active alerts";
}

static int row_height(const Alert* alert, int width) {
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GSize size = graphics_text_layout_get_content_size(alert->message, font,
        GRect(0, 0, width - 12, 1024), GTextOverflowModeWordWrap, GTextAlignmentLeft);
    return 23 + size.h + 10;
}

static void refresh_list(void) {
    if (!s_rows) return;
    GRect view = list_viewport();
    int height = list_status() ? 40 : 0;
    if (connected()) {
        for (Alert* a = s_active; a; a = a->next) height += row_height(a, view.size.w);
    }
    GSize size = GSize(view.size.w, height > view.size.h ? height : view.size.h);
    layer_set_frame(s_rows, (GRect){.size = size});
    scroll_layer_set_content_size(s_scroll, size);
    layer_mark_dirty(s_rows);
    layer_mark_dirty(s_header);
}

void alerts_connection_changed(void) {
    s_synced = false;
    refresh_list();
    ui_mark_dirty();
}

void alerts_sync(int sync) {
    if (sync == 2) {
        while (s_active) {
            Alert* next = s_active->next;
            retire_alert(s_active, false);
            s_active = next;
        }
        s_out_of_memory = false;
    }
    s_synced = sync == 1;
    refresh_list();
}

void alerts_apply(int id, int level, const char* message, int notify) {
    if (id <= 0 || level < ALERT_CLEAR || level > ALERT_EMERGENCY) return;
    Alert** link = &s_active;
    while (*link && (*link)->id != id) link = &(*link)->next;
    Alert* a = *link;
    if (a) *link = a->next;
    if (level == ALERT_CLEAR) {
        if (!a) return;
        retire_alert(a, true);
    } else {
        if (!a) {
            a = calloc(1, sizeof(*a));
            if (!a) {
                s_out_of_memory = true;
                refresh_list();
                return;
            }
            a->id = id;
        }
        a->level = level;
        snprintf(a->message, sizeof(a->message), "%s", message && *message ? message : level_name(level));
        link = &s_active;
        while (*link && ((*link)->level > a->level ||
               ((*link)->level == a->level && (*link)->id < id))) link = &(*link)->next;
        a->next = *link;
        *link = a;
        if (a->level > a->peak) {
            a->peak = a->level;
            if (notify > 0 && connected()) announce(a);
        }
        if (a == s_notice) refresh_notice(false);
    }
    refresh_list();
}

static void draw_header(Layer* layer, GContext* ctx) {
    const GRect b = layer_get_bounds(layer);
    const int inset = PBL_IF_ROUND_ELSE(60, 8);
    const int y = PBL_IF_ROUND_ELSE(18, 1);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    text(ctx, "Alerts", font, GColorWhite, GRect(inset, y, 75, 30), GTextAlignmentLeft);
    if (connected() && s_synced) {
        unsigned count = 0;
        for (Alert* a = s_active; a; a = a->next) ++count;
        char total[12];
        snprintf(total, sizeof(total), "%u", count);
        text(ctx, total, font, GColorLightGray, GRect(b.size.w - inset - 52, y, 52, 30), GTextAlignmentRight);
    }
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(inset, y + 32), GPoint(b.size.w - inset - 1, y + 32));
}

static void draw_rows(Layer* layer, GContext* ctx) {
    const int w = layer_get_bounds(layer).size.w;
    const char* status = list_status();
    int y = 0;
    if (status) {
        text(ctx, status, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GColorLightGray,
             GRect(2, y, w - 4, 34), GTextAlignmentLeft);
        y += 40;
    }
    if (!connected()) return;
    for (Alert* a = s_active; a; a = a->next) {
        int height = row_height(a, w);
        GColor accent = level_color(a->level);
        graphics_context_set_fill_color(ctx, accent);
        graphics_fill_rect(ctx, GRect(0, y + 5, 3, 15), 0, GCornerNone);
        text(ctx, level_name(a->level), fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), accent,
             GRect(10, y + 2, w - 12, 19), GTextAlignmentLeft);
        text(ctx, a->message, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), GColorWhite,
             GRect(10, y + 19, w - 12, height - 20), GTextAlignmentLeft);
        y += height;
        graphics_context_set_stroke_color(ctx, GColorDarkGray);
        graphics_draw_line(ctx, GPoint(10, y - 2), GPoint(w - 2, y - 2));
    }
}

static void list_load(Window* window) {
    Layer* root = window_get_root_layer(window);
    GRect view = list_viewport();
    s_header = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_header, draw_header);
    layer_add_child(root, s_header);
    s_scroll = scroll_layer_create(view);
    layer_add_child(root, scroll_layer_get_layer(s_scroll));
    scroll_layer_set_shadow_hidden(s_scroll, true);
    s_rows = layer_create((GRect){.size = view.size});
    layer_set_update_proc(s_rows, draw_rows);
    scroll_layer_add_child(s_scroll, s_rows);
    scroll_layer_set_click_config_onto_window(s_scroll, window);
    refresh_list();
}

static void list_unload(Window* window) {
    layer_destroy(s_rows);
    scroll_layer_destroy(s_scroll);
    layer_destroy(s_header);
    s_rows = NULL;
    s_scroll = NULL;
    s_header = NULL;
}

void alerts_open(ClickRecognizerRef recognizer, void* context) {
    window_stack_push(s_list, true);
}

void alerts_init(void) {
    s_notice_window = window_create();
    window_set_background_color(s_notice_window, GColorBlack);
    window_set_window_handlers(s_notice_window, (WindowHandlers){.load = notice_load, .unload = notice_unload});
    s_list = window_create();
    window_set_background_color(s_list, GColorBlack);
    window_set_window_handlers(s_list, (WindowHandlers){.load = list_load, .unload = list_unload});
}

void alerts_deinit(void) {
    window_destroy(s_notice_window);
    while (s_notice) release_notice();
    alerts_sync(2);
    window_destroy(s_list);
}
