#include "ui.h"

#include "buttons.h"
#include "protocol.h"
#include "state.h"

static Window* s_window;
static Layer* s_canvas;
static AppTimer* s_feedback_timer;

// Engaged = any active autopilot mode (standby/unknown render as "off").
static bool ap_engaged(int s) {
    return s != AP_STATE_STANDBY && s != AP_STATE_UNKNOWN;
}

// Autopilot status word shown in the header (and reflected in the background
// color): the active mode when engaged, "OFF" on standby/unknown.
static const char* ap_status_text(int s) {
    switch (s) {
        case AP_STATE_AUTO:  return "AUTO";
        case AP_STATE_WIND:  return "WIND";
        case AP_STATE_ROUTE: return "ROUTE";
        default:             return "OFF";
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

// Redraw once a minute so the clock stays current.
static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
    ui_mark_dirty();
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

// Draws a numeric value with its unit beside it (unit in a smaller font), the
// pair centered within [x, x+w]. The unit is bottom-aligned to the value
// (right for "kn"); pass unit_top for a superscript unit (right for "°", which
// the numbers-only value font can't render itself).
static void draw_value_unit(GContext* ctx, const char* value, const char* unit,
                            GFont vfont, GFont ufont, int x, int y, int w, bool unit_top) {
    const GSize vs = graphics_text_layout_get_content_size(
        value, vfont, GRect(0, 0, w, 44), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    const GSize us = graphics_text_layout_get_content_size(
        unit, ufont, GRect(0, 0, w, 44), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    const int gap = 4;
    const int sx = x + (w - (vs.w + gap + us.w)) / 2;
    const int uy = unit_top ? y + 2 : y + (vs.h - us.h) - 2;
    graphics_draw_text(ctx, value, vfont, GRect(sx, y, vs.w + 8, vs.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, unit, ufont, GRect(sx + vs.w + gap, uy, us.w + 8, us.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Rendered width of a string in a given font.
static int text_width(const char* s, GFont f) {
    return graphics_text_layout_get_content_size(
               s, f, GRect(0, 0, 140, 44), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft)
        .w;
}

// Rendered width of a value + unit pair, matching draw_value_unit's 4px gap.
static int value_unit_width(const char* value, const char* unit, GFont vf, GFont uf) {
    return text_width(value, vf) + 4 + text_width(unit, uf);
}

// Compact wind wheel: a circle with a small bow tick at the top, a small red
// direction tip riding just inside the rim (pointing outward at the apparent-wind
// angle), and the wind speed ("12.3 kn") centered inside the circle. Drawn in the
// foreground color (fg) so it stays legible on both the black and green
// backgrounds.
static void draw_wind_wheel(GContext* ctx, int cx, int cy, int radius, int wind_angle,
                            int wind_speed, GFont f_val, GFont f_unit, GColor fg) {
    const GPoint center = GPoint(cx, cy);
    graphics_context_set_stroke_color(ctx, fg);
    graphics_draw_circle(ctx, center, radius);
    graphics_draw_line(ctx, GPoint(center.x, center.y - radius),
                       GPoint(center.x, center.y - radius + 6));

    // Just the arrow tip, near the rim. Built per-frame so it tracks the radius.
    const int tip = radius - 1;
    const int base = radius - 13;
    GPoint pts[3] = {{0, -tip}, {6, -base}, {-6, -base}};
    GPathInfo info = {.num_points = 3, .points = pts};
    GPath* arrow = gpath_create(&info);
    gpath_rotate_to(arrow, DEG_TO_TRIGANGLE(wind_angle));
    gpath_move_to(arrow, center);
    graphics_context_set_fill_color(ctx, GColorRed);
    gpath_draw_filled(ctx, arrow);
    gpath_destroy(arrow);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d", wind_speed / 10, wind_speed % 10);
    graphics_context_set_text_color(ctx, fg);
    draw_value_unit(ctx, buf, "kn", f_val, f_unit, cx - radius, cy - 20, 2 * radius, false);
}

/*
 * Connected dashboard. HDG and SOG sit side by side as a centered top row (big
 * numbers with small °/kn units), and the wind wheel — a small red direction tip
 * near the rim plus the wind speed inside — is centered below. The autopilot
 * status is not drawn here: it lives in the header (canvas_update) and tints the
 * whole background (green when engaged, black when off), passed in as fg.
 *
 *   Both shapes (emery / gabbro)
 *   .----------------------------.
 *   | 14:32                  AUTO |  <- clock (left) + AP status (right)
 *   |----------------------------|  <- divider
 *   |     HDG          SOG       |
 *   |     142°        6.4 kn     |
 *   |            WIND            |
 *   |          _.-^-._          |
 *   |         ( 12.3 kn )        |
 *   |          "-.__.-"         |
 *   '----------------------------'
 *
 * Emery just uses a slightly smaller wheel to fit its narrower screen (on round
 * the corners would clip a wider layout anyway).
 */
static void draw_dashboard(GContext* ctx, const AppState* state, int w, GColor fg) {
    char hbuf[16], sbuf[16];
    GFont f_num = fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);  // all values
    GFont f_unit = fonts_get_system_font(FONT_KEY_GOTHIC_18);       // °/kn units
    GFont f_title = fonts_get_system_font(FONT_KEY_GOTHIC_14);      // HDG/SOG/WIND titles

    // ---- Top row: HDG | SOG, each sized to its content, centered as a pair. --
    snprintf(hbuf, sizeof(hbuf), "%d", state->heading);
    snprintf(sbuf, sizeof(sbuf), "%d.%d", state->sog / 10, state->sog % 10);
    const int hw = value_unit_width(hbuf, "°", f_num, f_unit);
    const int sw = value_unit_width(sbuf, "kn", f_num, f_unit);
    const int sep = PBL_IF_ROUND_ELSE(28, 20);
    const int gx = (w - (hw + sep + sw)) / 2;
    const int row_top = PBL_IF_ROUND_ELSE(54, 40);
    const int num_y = row_top + 16;

    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, "HDG", f_title, GRect(gx, row_top, hw, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_value_unit(ctx, hbuf, "°", f_num, f_unit, gx, num_y, hw, true);

    const int sog_x = gx + hw + sep;
    graphics_draw_text(ctx, "SOG", f_title, GRect(sog_x, row_top, sw, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_value_unit(ctx, sbuf, "kn", f_num, f_unit, sog_x, num_y, sw, false);

    // ---- Wind section: a single centered wheel with the speed inside -------
    const int wind_title_y = num_y + 40;
    graphics_context_set_text_color(ctx, fg);
    draw_row(ctx, "WIND", f_title, wind_title_y, 18, w);

    const int radius = PBL_IF_ROUND_ELSE(46, 44);
    const int cy = wind_title_y + 20 + radius;
    draw_wind_wheel(ctx, w / 2, cy, radius, state->wind_angle, state->wind_speed, f_num, f_unit, fg);
}

static void canvas_update(Layer* layer, GContext* ctx) {
    const AppState* state = app_state();
    const GRect b = layer_get_bounds(layer);
    const int w = b.size.w;

    // Autopilot status tints the whole screen: green when engaged, black off.
    // Text/graphics use the contrasting foreground color (fg).
    const bool engaged = ap_engaged(state->autopilot_state);
    const GColor bg = engaged ? GColorIslamicGreen : GColorBlack;
    const GColor fg = engaged ? GColorBlack : GColorWhite;
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    graphics_context_set_text_color(ctx, fg);

    GFont f_head = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

    // Header: clock bound left, autopilot status bound right, on both the
    // dashboard and status screens. A divider separates it from the rest. Both
    // are inset (more so on round, where the corners clip the edges).
    const int hdr_inset = PBL_IF_ROUND_ELSE(54, 6);
    const int hdr_y = PBL_IF_ROUND_ELSE(20, 4);
    const int hdr_box = w - 2 * hdr_inset;
    char timebuf[16];
    clock_copy_time_string(timebuf, sizeof(timebuf));
    graphics_draw_text(ctx, timebuf, f_head, GRect(hdr_inset, hdr_y, hdr_box, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, ap_status_text(state->autopilot_state), f_head,
                       GRect(hdr_inset, hdr_y, hdr_box, 28),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    const int div_y = PBL_IF_ROUND_ELSE(48, 34);
    graphics_context_set_stroke_color(ctx, fg);
    graphics_draw_line(ctx, GPoint(hdr_inset, div_y), GPoint(w - hdr_inset, div_y));

    // Connection status screen replaces the dashboard until connected.
    if (state->conn != CONN_CONNECTED) {
        draw_row(ctx, conn_text(state->conn), f_head, b.size.h / 2 - 52, 120, w);
        return;
    }

    draw_dashboard(ctx, state, w, fg);

    // Transient feedback pill (button pressed / command acked).
    if (state->feedback[0]) {
        const GRect pill = GRect(w / 2 - 44, b.size.h - PBL_IF_ROUND_ELSE(34, 26), 88, 24);
        graphics_context_set_fill_color(ctx, GColorBlue);
        graphics_fill_rect(ctx, pill, 6, GCornersAll);
        graphics_context_set_text_color(ctx, GColorWhite);
        graphics_draw_text(ctx, state->feedback, fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(pill.origin.x, pill.origin.y + 1, pill.size.w, pill.size.h),
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
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_click_config_provider(s_window, buttons_click_config_provider);
    window_set_window_handlers(s_window, (WindowHandlers){
                                             .load = window_load,
                                             .unload = window_unload,
                                         });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

void ui_deinit(void) {
    tick_timer_service_unsubscribe();
    if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
    window_destroy(s_window);
}
