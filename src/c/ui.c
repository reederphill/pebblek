#include "ui.h"

#include "alerts.h"
#include "buttons.h"
#include "comm.h"
#include "dial.h"
#include "instrument_ring.h"
#include "protocol.h"
#include "state.h"

static AppTimer* s_feedback_timer;
static char s_feedback[16];
static Window* s_window;
static Window* s_pilot_window;
static Layer* s_canvas;
static Layer* s_pilot_canvas;
static GFont s_instrument_font;

bool ui_is_pilot(void) {
    return s_pilot_window && window_stack_get_top_window() == s_pilot_window;
}

// Autopilot status word shown in the caption: the active mode when engaged, "OFF"
// on standby/unknown.
static const char* ap_status_text(int s) {
    switch (s) {
        case AP_STATE_AUTO:  return "AUTO";
        case AP_STATE_WIND:  return "WIND";
        case AP_STATE_ROUTE: return "ROUTE";
        case AP_STATE_STANDBY: return "OFF";
        default:             return "--";
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
    if (s_pilot_canvas) layer_mark_dirty(s_pilot_canvas);
}

// Redraw once a minute so the clock stays current.
static void tick_handler(struct tm* tick_time, TimeUnits units_changed) {
    ui_mark_dirty();
}

static void clear_feedback(void* data) {
    s_feedback_timer = NULL;
    s_feedback[0] = 0;
    ui_mark_dirty();
}

void ui_set_feedback(const char* label) {
    snprintf(s_feedback, sizeof(s_feedback), "%s", label);
    if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
    s_feedback_timer = app_timer_register(4000, clear_feedback, NULL);
    ui_mark_dirty();
}

// ---- rendering ------------------------------------------------------------

// Wrap degrees into -179..180 (negative = port / left of the bow).
static int wrap180(int deg) {
    deg = ((deg % 360) + 360) % 360;
    return deg > 180 ? deg - 360 : deg;
}

// ---- the big number ---------------------------------------------------------
//
// Roboto Bold, digits only, bundled as a custom font resource (resources/fonts/,
// declared in package.json). Loaded once with the window.
static GFont s_num_font;

#ifdef PBL_ROUND
// Where the digits sit inside the font's text box, measured from a screenshot: the
// glyphs start DIGIT_TOP below the box's top and are DIGIT_H tall.
#define NUM_BOX_H 110
#define DIGIT_TOP 27
#define DIGIT_H 70

// Draws the digit string in the current text color, horizontally centered in
// region, with the text box's top at region.origin.y. Returns the x where the
// digits end.
static int draw_number(GContext* ctx, const char* digits, GRect region) {
    const GSize sz = graphics_text_layout_get_content_size(
        digits, s_num_font, GRect(0, 0, 200, NUM_BOX_H), GTextOverflowModeTrailingEllipsis,
        GTextAlignmentLeft);
    const int x = region.origin.x + (region.size.w - sz.w) / 2;
    graphics_draw_text(ctx, digits, s_num_font, GRect(x, region.origin.y, sz.w + 8, NUM_BOX_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return x + sz.w;
}

#endif

// Pilot readout, inside the wind ring on the rectangular watch.
typedef struct {
    char digits[8];   // the number: a diff / wind angle magnitude, or a heading
    char label[32];   // under the number: which side, or what will be sent
    char target[12];  // "145°": the true desired heading, next to the label
    int side;         // +1 starboard (green), -1 port (red), 0 none (white)
    bool pending;     // label is a pending-change prompt (drawn plain, no arrow)
} Readout;

static void set_signed(Readout* r, int deg) {
    r->side = deg > 0 && deg != 180 ? 1 : (deg < 0 && deg != -180 ? -1 : 0);
    snprintf(r->digits, sizeof(r->digits), "%d", deg < 0 ? -deg : deg);
}

// What to show, by mode:
//   AUTO:    how far the boat's heading is from the desired heading (green = the
//            target is to starboard, red = to port), with the true desired
//            heading ("145°") next to the label
//   WIND:    the set apparent wind angle (green starboard, red port)
//   other:   the current heading, in white, so the screen is never empty
// While a change is pending (touch drag or a batch of button presses) the label
// says what SELECT will send, and the number tracks the new *absolute* desired
// heading (AUTO; coloured by which way the change turns) or set angle (WIND).
static Readout readout(const AppState* state) {
    Readout r = {"", "", "", 0, false};
    const int delta = state->dial_active ? state->dial_delta : 0;
    const bool auto_mode =
        state->autopilot_state == AP_STATE_AUTO && state->target_heading != TARGET_NONE;
    const bool wind_mode =
        state->autopilot_state == AP_STATE_WIND && state->target_wind != TARGET_NONE;
    if (!state_fresh(FIELD_AUTOPILOT) ||
        (auto_mode && (!state_fresh(FIELD_TARGET_HEADING) || !state_fresh(FIELD_HEADING))) ||
        (wind_mode && !state_fresh(FIELD_TARGET_WIND)) ||
        (!auto_mode && !wind_mode && !state_fresh(FIELD_HEADING))) {
        snprintf(r.label, sizeof(r.label), "DATA STALE");
        return r;
    }
    if (!auto_mode && !wind_mode) {
        snprintf(r.digits, sizeof(r.digits), "%03d", state->heading);
        snprintf(r.label, sizeof(r.label), "HEADING");
        return r;
    }

    const int new_target = ((state->target_heading + delta) % 360 + 360) % 360;
    if (auto_mode && state->dial_active) {
        snprintf(r.digits, sizeof(r.digits), "%03d", new_target);
        r.side = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
    } else if (auto_mode) {
        set_signed(&r, wrap180(state->target_heading - state->heading));
#ifdef PBL_ROUND
        snprintf(r.target, sizeof(r.target), "%03d°", state->target_heading);
#endif
    } else {
        set_signed(&r, wrap180(state->target_wind + delta));
    }

    if (state->dial_active) {
        r.pending = true;
        if (delta == 0) {
            snprintf(r.label, sizeof(r.label), "DRAG TO SET");
        } else {
            snprintf(r.label, sizeof(r.label), "SEND %c%d°", delta > 0 ? '+' : '-',
                     delta > 0 ? delta : -delta);
        }
    } else if (r.side > 0) {
        snprintf(r.label, sizeof(r.label), "STBD");
    } else if (r.side < 0) {
        snprintf(r.label, sizeof(r.label), "PORT");
    } else {
        snprintf(r.label, sizeof(r.label), "%s",
                 !strcmp(r.digits, "180") ? "ASTERN" : (auto_mode ? "ON COURSE" : "AHEAD"));
    }
    return r;
}

// A small filled triangle centered at (cx, cy) pointing left (dir < 0) or right.
static void draw_arrow(GContext* ctx, int cx, int cy, int dir) {
    GPoint pts[3] = {{dir * 7, 0}, {-dir * 5, -8}, {-dir * 5, 8}};
    GPathInfo info = {.num_points = 3, .points = pts};
    GPath* path = gpath_create(&info);
    gpath_move_to(path, GPoint(cx, cy));
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
}

static int text_w(const char* t, GFont f, int w) {
    return graphics_text_layout_get_content_size(
               t, f, GRect(0, 0, w, 30), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft)
        .w;
}

// The row under the number, centered as one group: the label (for "PORT" / "STBD"
// with an arrow on the matching side, in the side's color), then the true desired
// heading in smaller type ("145°").
static void draw_label(GContext* ctx, const Readout* r, GColor color, int y, int w) {
    GFont f = fonts_get_system_font(PBL_IF_ROUND_ELSE(FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_18_BOLD));
    const bool arrow = !r->pending && r->side != 0;
    const int arrow_w = 14, pad = 6, gap = 16;

    const int lw = text_w(r->label, f, w);
    const int ew = r->target[0] ? text_w(r->target, f, w) : 0;
    const int total = lw + (arrow ? arrow_w + pad : 0) + (ew ? gap + ew : 0);

    int x = (w - total) / 2;
    const int mid = y + PBL_IF_ROUND_ELSE(14, 12);
    graphics_context_set_text_color(ctx, GColorWhite);
    if (arrow && r->side < 0) {
        graphics_context_set_fill_color(ctx, color);
        draw_arrow(ctx, x + arrow_w / 2, mid, -1);
        x += arrow_w + pad;
    }
    graphics_draw_text(ctx, r->label, f, GRect(x, y, lw + 6, 30),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    x += lw;
    if (arrow && r->side > 0) {
        graphics_context_set_fill_color(ctx, color);
        draw_arrow(ctx, x + pad + arrow_w / 2, mid, 1);
        x += pad + arrow_w;
    }
    if (ew) {
        graphics_draw_text(ctx, r->target, f, GRect(x + gap, y, ew + 6, 30),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    }
}

static void draw_header(GContext* ctx, const AppState* state, GRect b, bool ring_layout) {
    const int inset = PBL_IF_ROUND_ELSE(52, 6);
    const int y = PBL_IF_ROUND_ELSE(20, 4);
    char clock[16], pilot[24];
    clock_copy_time_string(clock, sizeof(clock));
    const bool fresh = state_fresh(FIELD_AUTOPILOT);
    snprintf(pilot, sizeof(pilot), "PILOT %s", fresh ? ap_status_text(state->autopilot_state) : "--");
    if (fresh && state->autopilot_state == AP_STATE_AUTO) {
        if (state_fresh(FIELD_TARGET_HEADING)) snprintf(pilot, sizeof(pilot), "AUTO %03d°", state->target_heading);
        else snprintf(pilot, sizeof(pilot), "AUTO --");
    } else if (fresh && state->autopilot_state == AP_STATE_WIND) {
        const int angle = wrap180(state->target_wind);
        if (ring_layout && state_fresh(FIELD_TARGET_WIND)) snprintf(pilot, sizeof(pilot), "WIND %d°", angle);
        else if (state_fresh(FIELD_TARGET_WIND)) snprintf(pilot, sizeof(pilot), "WIND %d°%s", abs(angle),
                                                    angle < 0 ? "P" : (angle > 0 && angle < 180 ? "S" : ""));
        else snprintf(pilot, sizeof(pilot), "WIND --");
    }
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, clock, font, GRect(inset, y, 56, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, fresh ? GColorWhite : GColorLightGray);
    graphics_draw_text(ctx, pilot, font, GRect(inset + 56, y, b.size.w - 2 * inset - 56, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    if (!ring_layout) graphics_draw_line(ctx, GPoint(inset, y + 28), GPoint(b.size.w - inset, y + 28));
}

static void draw_dashboard(GContext* ctx, const AppState* state, GRect b) {
    const Readout r = readout(state);
    const GColor color = r.side > 0 ? GColorGreen : (r.side < 0 ? GColorRed : GColorWhite);
    const int w = b.size.w;

#ifdef PBL_ROUND
    const int inset = 6;
    const int div_y = PBL_IF_ROUND_ELSE(48, 34);

    // The number, with a degree mark to its upper right.
    const int num_top = div_y + 11;
    const int deg_w = 22;
    graphics_context_set_text_color(ctx, color);
    if (!r.digits[0]) {
        graphics_context_set_text_color(ctx, GColorLightGray);
        graphics_draw_text(ctx, "--", fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD),
                           GRect(0, num_top + 20, w, 60), GTextOverflowModeTrailingEllipsis,
                           GTextAlignmentCenter, NULL);
        draw_label(ctx, &r, GColorLightGray, num_top + 115, w);
        return;
    }
    const int end_x = draw_number(
        ctx, r.digits, GRect(inset + 4, num_top, w - 2 * inset - 8 - deg_w, NUM_BOX_H));
    graphics_context_set_fill_color(ctx, color);
    const GPoint ring = GPoint(end_x + 12, num_top + DIGIT_TOP + 8);
    graphics_fill_circle(ctx, ring, 8);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, ring, 4);

    draw_label(ctx, &r, color, num_top + DIGIT_TOP + DIGIT_H + 12, w);
#else
    const GPoint center = instrument_ring_draw_rim(ctx, b, state);
    char number[12];
    if (r.digits[0]) snprintf(number, sizeof(number), "%s°", r.digits);
    else snprintf(number, sizeof(number), "--");
    graphics_context_set_text_color(ctx, r.digits[0] ? color : GColorLightGray);
    graphics_draw_text(ctx, number, s_num_font, GRect(0, center.y - 43, w, 60),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    draw_label(ctx, &r, color, center.y + 12, w);
#endif
}

// ---- home instruments ----------------------------------------------------

static void centered(GContext* ctx, const char* text, GFont font, GRect rect) {
    graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
}

#ifdef PBL_ROUND
static void draw_instrument(GContext* ctx, GRect cell, const char* label,
                            int value, TelemetryField field, int angle, TelemetryField angle_field) {
    const bool fresh = state_fresh(field) && value >= 0;
    const bool wind = angle_field != FIELD_COUNT;
    const bool angle_fresh = wind && state_fresh(angle_field) && angle >= 0;
    char number[16], detail[24];
    if (fresh) snprintf(number, sizeof(number), "%d.%d", value / SPEED_SCALE, value % SPEED_SCALE);
    else snprintf(number, sizeof(number), "--");

    graphics_context_set_text_color(ctx, GColorLightGray);
    centered(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
             GRect(cell.origin.x, cell.origin.y, cell.size.w, 22));
    graphics_context_set_text_color(ctx, fresh ? GColorWhite : GColorDarkGray);
    // Fit unusual three-digit speeds without clipping the adjacent quadrant.
    GFont number_font = strlen(number) > 4 ? fonts_get_system_font(FONT_KEY_LECO_28_LIGHT_NUMBERS)
                                         : s_instrument_font;
    centered(ctx, number, number_font,
             GRect(cell.origin.x, cell.origin.y + 20, cell.size.w, 46));
    if (!fresh) {
        snprintf(detail, sizeof(detail), "%s", value < 0 ? "NO DATA" : "STALE");
    } else if (!wind) {
        snprintf(detail, sizeof(detail), "kn");
    } else if (!angle_fresh) {
        snprintf(detail, sizeof(detail), "kn / --");
    } else {
        const int signed_angle = wrap180(angle);
        snprintf(detail, sizeof(detail), "kn / %d° %s", abs(signed_angle),
                 signed_angle == 0 ? "" : (signed_angle == 180 ? "A" : (signed_angle < 0 ? "P" : "S")));
    }
    graphics_context_set_text_color(ctx, !fresh ? GColorLightGray :
        (angle_fresh && angle != 0 && angle != 180 ?
            (wrap180(angle) < 0 ? GColorRed : GColorGreen) : GColorWhite));
    centered(ctx, detail, fonts_get_system_font(FONT_KEY_GOTHIC_18),
             GRect(cell.origin.x, cell.origin.y + 62, cell.size.w, 23));
}

static void draw_instruments(GContext* ctx, const AppState* state, GRect b) {
    draw_header(ctx, state, b, false);
    const int inset = PBL_IF_ROUND_ELSE(34, 5);
    const int top = PBL_IF_ROUND_ELSE(50, 36);
    const int cell_h = PBL_IF_ROUND_ELSE(85, 86);
    const int mid = b.size.w / 2;
    const int cell_w = mid - inset - 3;
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_line(ctx, GPoint(mid, top + 5), GPoint(mid, top + 2 * cell_h - 4));
    graphics_draw_line(ctx, GPoint(inset, top + cell_h), GPoint(b.size.w - inset, top + cell_h));
    draw_instrument(ctx, GRect(inset, top, cell_w, cell_h), "SOW", state->stw, FIELD_STW, 0, FIELD_COUNT);
    draw_instrument(ctx, GRect(mid + 3, top, cell_w, cell_h), "SOG", state->sog, FIELD_SOG, 0, FIELD_COUNT);
    draw_instrument(ctx, GRect(inset, top + cell_h, cell_w, cell_h), "APPARENT",
                    state->wind_speed, FIELD_WIND_SPEED, state->wind_angle, FIELD_WIND_ANGLE);
    draw_instrument(ctx, GRect(mid + 3, top + cell_h, cell_w, cell_h), "TRUE",
                    state->true_wind_speed, FIELD_TRUE_WIND_SPEED, state->true_wind_angle, FIELD_TRUE_WIND_ANGLE);
    const char* footer = state->conn == CONN_CONNECTED ? "UP: PILOT" :
        (state->conn == CONN_NO_CONFIG ? "SET UP ON PHONE" :
         (state->conn == CONN_AWAITING_APPROVAL ? "APPROVE ON SERVER" :
          (state->conn == CONN_ACCESS_DENIED ? "ACCESS DENIED" :
           (state->conn == CONN_CONNECTING ? "CONNECTING..." : "DISCONNECTED"))));
    graphics_context_set_text_color(ctx, GColorLightGray);
    centered(ctx, footer, fonts_get_system_font(FONT_KEY_GOTHIC_14),
             GRect(0, top + 2 * cell_h + 1, b.size.w, 19));
}

#else
static void draw_instruments(GContext* ctx, const AppState* state, GRect b) {
    draw_header(ctx, state, b, true);
    instrument_ring_draw(ctx, b, state, s_instrument_font);
}
#endif

static void draw_page(Layer* layer, GContext* ctx) {
    const AppState* state = app_state();
    const GRect b = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    if (layer == s_canvas) {
        draw_instruments(ctx, state, b);
        return;
    }
    draw_header(ctx, state, b, PBL_IF_ROUND_ELSE(false, true));
    if (state->conn == CONN_CONNECTED) {
        draw_dashboard(ctx, state, b);
        return;
    }
    graphics_context_set_text_color(ctx, GColorWhite);
#ifdef PBL_ROUND
    centered(ctx, conn_text(state->conn), fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
             GRect(0, b.size.h / 2 - 42, b.size.w, 120));
#else
    const GPoint center = instrument_ring_draw_rim(ctx, b, state);
    centered(ctx, conn_text(state->conn), fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
             GRect(center.x - 70, center.y - 32, 140, 84));
#endif
}

static void canvas_update(Layer* layer, GContext* ctx) {
    draw_page(layer, ctx);
    if (!s_feedback[0]) return;
    GRect b = layer_get_bounds(layer);
    GRect pill = GRect(b.size.w / 2 - 48, b.size.h - PBL_IF_ROUND_ELSE(38, 26), 96, 24);
    graphics_context_set_fill_color(ctx, GColorBlue);
    graphics_fill_rect(ctx, pill, 5, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    centered(ctx, s_feedback, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), pill);
}

// Each screen owns a window, so the opening Up press cannot adjust the pilot.
static void open_pilot(ClickRecognizerRef recognizer, void* context) {
    window_stack_push(s_pilot_window, true);
}

static void home_click_config(void* context) {
    window_single_click_subscribe(BUTTON_ID_UP, open_pilot);
    window_single_click_subscribe(BUTTON_ID_DOWN, alerts_open);
}

static void pilot_disappear(Window* window) {
    buttons_cancel();
    dial_cancel();
    comm_cancel_pending();
}

static void window_load(Window* window) {
    Layer* root = window_get_root_layer(window);
    Layer* canvas = layer_create(layer_get_bounds(root));
    if (window == s_window) s_canvas = canvas;
    else s_pilot_canvas = canvas;
    layer_set_update_proc(canvas, canvas_update);
    layer_add_child(root, canvas);
}

static void window_unload(Window* window) {
    Layer** canvas = window == s_window ? &s_canvas : &s_pilot_canvas;
    layer_destroy(*canvas);
    *canvas = NULL;
}

void ui_init(void) {
    alerts_init();
    s_num_font = fonts_load_custom_font(resource_get_handle(
        PBL_IF_ROUND_ELSE(RESOURCE_ID_FONT_ROBOTO_BOLD_96, RESOURCE_ID_FONT_ROBOTO_BOLD_48)));
    s_instrument_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ROBOTO_BOLD_36));
    s_window = window_create();
    s_pilot_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_background_color(s_pilot_window, GColorBlack);
    window_set_click_config_provider(s_window, home_click_config);
    window_set_click_config_provider(s_pilot_window, buttons_click_config_provider);
    window_set_window_handlers(s_window, (WindowHandlers){.load = window_load, .unload = window_unload});
    window_set_window_handlers(s_pilot_window, (WindowHandlers){
        .load = window_load, .unload = window_unload, .disappear = pilot_disappear});
    window_stack_push(s_window, true);
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

void ui_deinit(void) {
    tick_timer_service_unsubscribe();
    if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
    alerts_deinit();
    window_destroy(s_pilot_window);
    window_destroy(s_window);
    fonts_unload_custom_font(s_num_font);
    fonts_unload_custom_font(s_instrument_font);
}
