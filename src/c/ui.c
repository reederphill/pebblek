#include "ui.h"

#include "buttons.h"
#include "protocol.h"
#include "state.h"

static Window* s_window;
static Layer* s_canvas;
static GPath* s_arrow;
static AppTimer* s_feedback_timer;

// Apparent-wind arrow, pointing up (bow) before rotation. Sized for the large
// dial (tip ~32px from center, short tail for a clear direction read).
static const GPathInfo ARROW_POINTS = {
    .num_points = 4,
    .points = (GPoint[]){{0, -32}, {9, 9}, {0, 2}, {-9, 9}},
};

// Engaged = any active autopilot mode (standby/unknown render as "off").
static bool ap_engaged(int s) {
    return s != AP_STATE_STANDBY && s != AP_STATE_UNKNOWN;
}

static const char* ap_label(int s) {
    switch (s) {
        case AP_STATE_AUTO:  return "AUTO";
        case AP_STATE_WIND:  return "WIND";
        case AP_STATE_ROUTE: return "ROUTE";
        default:             return "OFF";
    }
}

static const char* ap_glyph(int s) {
    switch (s) {
        case AP_STATE_AUTO:  return "A";
        case AP_STATE_WIND:  return "W";
        case AP_STATE_ROUTE: return "R";
        default:             return "";
    }
}

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

/*
 * Connected dashboard layout. The autopilot square is green when engaged and
 * dark gray on standby; the dial arrow points in the apparent wind direction.
 * On round, the rounded corners clip the edges, so the heading + square are
 * centered as a group instead of hugging the left/right edges.
 *
 *   Rectangular (emery)            Round (gabbro)
 *   +----------------------+        .--------------------.
 *   | HDG           +----+ |      /     HDG     +----+    \
 *   |  142          | A  | |     |     142      | A  |     |
 *   |               +----+ |     |              +----+     |
 *   |                AUTO  |    |                 AUTO       |
 *   |    _.-""-._   WIND   |    |    _.-""-._    WIND       |
 *   |  (    ^    )  12.3   |    |  (    ^    )   12.3       |
 *   |  (    |    )  kn     |    |  (    |    )   kn         |
 *   |    "-.__.-"          |     |    "-.__.-"             |
 *   |                      |      \                       /
 *   |     SOG  6.4 kn      |       '     SOG  6.4 kn     '
 *   +----------------------+        '--------------------'
 */
static void canvas_update(Layer* layer, GContext* ctx) {
    const AppState* state = app_state();
    const GRect b = layer_get_bounds(layer);
    const int w = b.size.w;
    const int h = b.size.h;
    char buf[24];

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);

    GFont f_big = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    GFont f_speed = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    GFont f_med = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    GFont f_small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    GFont f_tiny = fonts_get_system_font(FONT_KEY_GOTHIC_14);

    // Connection status screen replaces the dashboard until connected.
    if (state->conn != CONN_CONNECTED) {
        draw_row(ctx, conn_text(state->conn), f_med, b.size.h / 2 - 52, 120, w);
        return;
    }

    const int top = PBL_IF_ROUND_ELSE(24, 8);

    // ---- Heading (big number) + autopilot square --------------------------
    // Rectangular: heading hugs the left edge, square the right edge. Round:
    // the rounded corners clip the edges, so center the heading+square as a
    // group instead.
    const int sq = 42;
    const int head_w = 96;  // fits up to "359°"
    const int grp_x = (w - (head_w + 8 + sq)) / 2;  // round: centered group
    const int head_x = PBL_IF_ROUND_ELSE(grp_x, 8);
    const GTextAlignment head_align = PBL_IF_ROUND_ELSE(GTextAlignmentRight, GTextAlignmentLeft);
    const int sq_x = PBL_IF_ROUND_ELSE(grp_x + head_w + 8, w - 8 - sq);
    const int sq_y = top + 14;

    graphics_draw_text(ctx, "HDG", f_tiny, GRect(head_x, top, head_w, 16),
                       GTextOverflowModeTrailingEllipsis, head_align, NULL);
    snprintf(buf, sizeof(buf), "%d°", state->heading);
    graphics_draw_text(ctx, buf, f_big, GRect(head_x, top + 12, head_w, 46),
                       GTextOverflowModeTrailingEllipsis, head_align, NULL);

    const bool engaged = ap_engaged(state->autopilot_state);
    const GRect sq_r = GRect(sq_x, sq_y, sq, sq);
    graphics_context_set_fill_color(ctx, engaged ? GColorGreen : GColorDarkGray);
    graphics_fill_rect(ctx, sq_r, 6, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_round_rect(ctx, sq_r, 6);
    graphics_context_set_text_color(ctx, engaged ? GColorBlack : GColorWhite);
    graphics_draw_text(ctx, ap_glyph(state->autopilot_state), f_med,
                       GRect(sq_x, sq_y + 5, sq, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, engaged ? GColorGreen : GColorLightGray);
    graphics_draw_text(ctx, ap_label(state->autopilot_state), f_tiny,
                       GRect(sq_x - 10, sq_y + sq + 1, sq + 20, 14),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // ---- Wind: big dial (apparent direction) + speed grouped beside it -----
    const int radius = PBL_IF_ROUND_ELSE(36, 40);
    const int cx = PBL_IF_ROUND_ELSE(w / 2 - 30, 8 + radius + 4);
    const int cy = top + PBL_IF_ROUND_ELSE(56, 72) + radius;
    const GPoint center = GPoint(cx, cy);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_circle(ctx, center, radius);
    graphics_draw_line(ctx, GPoint(center.x, center.y - radius),
                       GPoint(center.x, center.y - radius + 8));
    gpath_rotate_to(s_arrow, DEG_TO_TRIGANGLE(state->wind_angle));
    gpath_move_to(s_arrow, center);
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, s_arrow);

    const int wx = cx + radius + 10;
    graphics_context_set_text_color(ctx, GColorWhite);
    snprintf(buf, sizeof(buf), "%d.%d", state->wind_speed / 10, state->wind_speed % 10);
    graphics_draw_text(ctx, buf, f_speed, GRect(wx, cy - 28, w - wx - 4, 34),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "WIND kn", f_tiny, GRect(wx, cy + 8, w - wx - 4, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // ---- SOG: own row, same number size as wind speed (equal prominence) ---
    const int sog_y = h - PBL_IF_ROUND_ELSE(56, 48);
    snprintf(buf, sizeof(buf), "%d.%d", state->sog / 10, state->sog % 10);
    graphics_draw_text(ctx, buf, f_speed, GRect(0, sog_y, w, 34),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, "SOG kn", f_tiny, GRect(0, sog_y + 30, w, 16),
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
