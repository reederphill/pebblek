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

// Text shown inside the autopilot box: the mode initial when engaged, "OFF"
// otherwise (the box itself is the legend — no separate caption needed).
static const char* ap_box_text(int s) {
    switch (s) {
        case AP_STATE_AUTO:  return "A";
        case AP_STATE_WIND:  return "W";
        case AP_STATE_ROUTE: return "R";
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

// Draws a numeric value with its unit beside it (unit in a smaller font,
// bottom-aligned to the value), the pair centered within [x, x+w].
static void draw_value_unit(GContext* ctx, const char* value, const char* unit,
                            GFont vfont, GFont ufont, int x, int y, int w) {
    const GSize vs = graphics_text_layout_get_content_size(
        value, vfont, GRect(0, 0, w, 44), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    const GSize us = graphics_text_layout_get_content_size(
        unit, ufont, GRect(0, 0, w, 44), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    const int gap = 4;
    const int sx = x + (w - (vs.w + gap + us.w)) / 2;
    graphics_draw_text(ctx, value, vfont, GRect(sx, y, vs.w + 8, vs.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, unit, ufont,
                       GRect(sx + vs.w + gap, y + (vs.h - us.h) - 2, us.w + 8, us.h),
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

// Autopilot state square: green + mode initial (A/W/R) when engaged, dark gray +
// "OFF" on standby. Its white outline is the legend, so no caption is needed.
static void draw_ap_square(GContext* ctx, const AppState* state, int x, int y, int sq,
                           GFont f_med, GFont f_small) {
    const bool engaged = ap_engaged(state->autopilot_state);
    const GRect sq_r = GRect(x, y, sq, sq);
    graphics_context_set_fill_color(ctx, engaged ? GColorGreen : GColorDarkGray);
    graphics_fill_rect(ctx, sq_r, 6, GCornersAll);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_draw_round_rect(ctx, sq_r, 6);
    graphics_context_set_text_color(ctx, engaged ? GColorBlack : GColorWhite);
    const char* txt = ap_box_text(state->autopilot_state);
    GFont f = engaged ? f_med : f_small;
    const GSize ts = graphics_text_layout_get_content_size(
        txt, f, GRect(0, 0, sq, sq), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter);
    // Center vertically; the -3 corrects Pebble's font top-line padding so caps
    // sit in the visual middle of the box.
    const int ty = y + (sq - ts.h) / 2 - 3;
    graphics_draw_text(ctx, txt, f, GRect(x, ty, sq, ts.h + 6),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// Compact wind wheel: a circle with a small bow tick at the top, a small red
// direction tip riding just inside the rim (pointing outward at the apparent-wind
// angle), and the wind speed ("12.3 kn") centered inside the circle.
static void draw_wind_wheel(GContext* ctx, int cx, int cy, int radius, int wind_angle,
                            int wind_speed, GFont f_val, GFont f_unit) {
    const GPoint center = GPoint(cx, cy);
    graphics_context_set_stroke_color(ctx, GColorWhite);
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
    draw_value_unit(ctx, buf, "kn", f_val, f_unit, cx - radius, cy - 16, 2 * radius);
}

/*
 * Connected dashboard, per display shape. The autopilot square is green with the
 * mode initial (A/W/R) when engaged, dark gray with "OFF" on standby. The wind
 * wheel shows a small red tip near the rim in the apparent-wind direction with
 * the wind speed centered inside it.
 *
 *   Both shapes (emery / gabbro)
 *   .----------------------------.
 *   |           14:32            |  <- clock (drawn by canvas_update)
 *   |---------------------------- |  <- divider
 *   |  HDG    SOG       +----+    |
 *   |  142    6.4kn     | A  |    |
 *   |                   +----+    |
 *   |            WIND            |
 *   |           _.-^-._          |
 *   |          / 12.3  \         |
 *   |          \  kn   /         |
 *   |           "-.__.-"         |
 *   '----------------------------'
 *
 * The clock + divider across the top are drawn by canvas_update (on every screen,
 * not just the dashboard). Below that, HDG + SOG + the square form a centered top
 * row and the wind wheel is centered under it. Emery just uses slightly smaller
 * columns/wheel to fit its narrower screen (on round the corners would clip a
 * wider layout anyway).
 */
static void draw_dashboard(GContext* ctx, const AppState* state, int w) {
    char hbuf[12], sbuf[12];
    GFont f_num = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);   // all values
    GFont f_title = fonts_get_system_font(FONT_KEY_GOTHIC_14);      // all titles + units
    GFont f_med = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);   // autopilot mode letter

    const int sq = 42;

    // ---- Top row: HDG | SOG | square. Each box is sized to its own content and
    //      the three are laid out with equal gaps, centered as a group, so they
    //      read as evenly spaced regardless of the current values. ------------
    snprintf(hbuf, sizeof(hbuf), "%d°", state->heading);
    snprintf(sbuf, sizeof(sbuf), "%d.%d", state->sog / 10, state->sog % 10);
    const int hw = text_width(hbuf, f_num);
    const int sw = value_unit_width(sbuf, "kn", f_num, f_title);
    const int sep = PBL_IF_ROUND_ELSE(16, 12);
    const int gx = (w - (hw + sep + sw + sep + sq)) / 2;
    const int row_top = 44;
    const int num_y = row_top + 16;

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "HDG", f_title, GRect(gx, row_top, hw, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    graphics_draw_text(ctx, hbuf, f_num, GRect(gx, num_y, hw, 32),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    const int sog_x = gx + hw + sep;
    graphics_draw_text(ctx, "SOG", f_title, GRect(sog_x, row_top, sw, 16),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_value_unit(ctx, sbuf, "kn", f_num, f_title, sog_x, num_y, sw);

    draw_ap_square(ctx, state, sog_x + sw + sep, row_top + 10, sq, f_med, f_title);

    // ---- Wind section: a single centered wheel with the speed inside -------
    const int wind_title_y = 104;
    graphics_context_set_text_color(ctx, GColorWhite);
    draw_row(ctx, "WIND", f_title, wind_title_y, 18, w);

    const int radius = PBL_IF_ROUND_ELSE(48, 42);
    const int cy = wind_title_y + 22 + radius;
    draw_wind_wheel(ctx, w / 2, cy, radius, state->wind_angle, state->wind_speed, f_num, f_title);
}

static void canvas_update(Layer* layer, GContext* ctx) {
    const AppState* state = app_state();
    const GRect b = layer_get_bounds(layer);
    const int w = b.size.w;

    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);

    GFont f_med = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);

    // Clock across the very top, shown on both the dashboard and status screens,
    // with a divider line separating it from the rest of the screen. On round the
    // line is inset so its ends don't disappear into the clipped corners.
    char timebuf[16];
    clock_copy_time_string(timebuf, sizeof(timebuf));
    draw_row(ctx, timebuf, f_med, PBL_IF_ROUND_ELSE(6, 2), 28, w);
    const int div_y = PBL_IF_ROUND_ELSE(37, 33);
    const int div_inset = PBL_IF_ROUND_ELSE(44, 8);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, GPoint(div_inset, div_y), GPoint(w - div_inset, div_y));

    // Connection status screen replaces the dashboard until connected.
    if (state->conn != CONN_CONNECTED) {
        draw_row(ctx, conn_text(state->conn), f_med, b.size.h / 2 - 52, 120, w);
        return;
    }

    draw_dashboard(ctx, state, w);

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
