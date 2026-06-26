#include "ui.h"

#include "buttons.h"
#include "protocol.h"
#include "state.h"

static Window* s_window;
static Layer* s_canvas;
static GPath* s_arrow;
static AppTimer* s_feedback_timer;

static const GPathInfo ARROW_POINTS = {
    .num_points = 4,
    .points = (GPoint[]){{0, -16}, {6, 6}, {0, 1}, {-6, 6}},
};

// ---- helpers --------------------------------------------------------------

static const char* conn_text(int conn) {
    switch (conn) {
        case CONN_NO_CONFIG:
            return "Open pebblek\nsettings on\nyour phone";
        case CONN_CONNECTING:
            return "Connecting...";
        case CONN_AWAITING_APPROVAL:
            return "Waiting for\napproval on\nSignalK server";
        case CONN_ACCESS_DENIED:
            return "Access denied";
        case CONN_DISCONNECTED:
        default:
            return "Disconnected\nReconnecting...";
    }
}

void ui_mark_dirty(void) {
    if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---- feedback (optimistic button + command ack) ---------------------------

static void feedback_timeout(void* data) {
    s_feedback_timer = NULL;
    app_state()->feedback[0] = '\0';
    ui_mark_dirty();
}

void ui_set_feedback(const char* label) {
    AppState* state = app_state();
    strncpy(state->feedback, label, sizeof(state->feedback) - 1);
    state->feedback[sizeof(state->feedback) - 1] = '\0';
    if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
    s_feedback_timer = app_timer_register(4000, feedback_timeout, NULL);
    ui_mark_dirty();
}

// ---- rendering ------------------------------------------------------------

static void draw_row(GContext* ctx, const char* text, GFont font, int y, int h, int w) {
    graphics_draw_text(ctx, text, font, GRect(0, y, w, h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer* layer, GContext* ctx) {
    const AppState* state = app_state();
    const GRect b = layer_get_bounds(layer);
    const int w = b.size.w;
    char buf[24];

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);

    GFont f_big = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    GFont f_med = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont f_small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    GFont f_tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);

    // Connection status screen replaces the dashboard until connected.
    if (state->conn != CONN_CONNECTED) {
        draw_row(ctx, conn_text(state->conn), f_med, b.size.h / 2 - 52, 120, w);
        return;
    }

    const int top = PBL_IF_ROUND_ELSE(20, 6);

    draw_row(ctx, "HEADING", f_tiny, top, 16, w);
    snprintf(buf, sizeof(buf), "%d°", state->heading);
    draw_row(ctx, buf, f_big, top + 13, 46, w);

    const bool engaged = (state->autopilot_state != AP_STATE_STANDBY &&
                          state->autopilot_state != AP_STATE_UNKNOWN);
    graphics_context_set_text_color(ctx, engaged ? GColorGreen : GColorLightGray);
    draw_row(ctx, engaged ? "AUTOPILOT ON" : "AUTOPILOT OFF", f_med, top + 60, 26, w);
    graphics_context_set_text_color(ctx, GColorWhite);

    snprintf(buf, sizeof(buf), "SOG %d.%d kn", state->sog / 10, state->sog % 10);
    draw_row(ctx, buf, f_small, top + 88, 22, w);

    // Wind: a bow-up dial with an arrow pointing in the apparent wind direction,
    // and the numeric speed alongside it.
    const int wind_y = top + 114;
    const GPoint center = GPoint(w / 2 - 38, wind_y + 24);
    const int radius = 22;
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_circle(ctx, center, radius);
    graphics_draw_line(ctx, GPoint(center.x, center.y - radius),
                       GPoint(center.x, center.y - radius + 6));
    gpath_rotate_to(s_arrow, DEG_TO_TRIGANGLE(state->wind_angle));
    gpath_move_to(s_arrow, center);
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, s_arrow);

    snprintf(buf, sizeof(buf), "%d.%d", state->wind_speed / 10, state->wind_speed % 10);
    graphics_draw_text(ctx, buf, f_med, GRect(w / 2, wind_y, w / 2 - 6, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "WIND kn", f_tiny, GRect(w / 2, wind_y + 28, w / 2 - 6, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Transient feedback pill (button pressed / command acked).
    if (state->feedback[0]) {
        const GRect pill = GRect(w / 2 - 44, b.size.h - PBL_IF_ROUND_ELSE(34, 26), 88, 24);
        graphics_context_set_fill_color(ctx, GColorBlue);
        graphics_fill_rect(ctx, pill, 6, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, state->feedback, f_small, GRect(pill.origin.x, pill.origin.y + 1, pill.size.w, pill.size.h),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
}

// ---- window lifecycle -----------------------------------------------------

static void window_load(Window* window) {
    Layer* root = window_get_root_layer(window);
    s_canvas = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_canvas, canvas_update);
    layer_add_child(root, s_canvas);
}

static void window_unload(Window* window) {
    layer_destroy(s_canvas);
    s_canvas = NULL;
}

void ui_init(void) {
    s_arrow = gpath_create(&ARROW_POINTS);

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_click_config_provider(s_window, buttons_click_config_provider);
    window_set_window_handlers(s_window, (WindowHandlers){
                                             .load = window_load,
                                             .unload = window_unload,
                                         });
    window_stack_push(s_window, true);
}

void ui_deinit(void) {
    if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
    window_destroy(s_window);
    gpath_destroy(s_arrow);
}
