#include "instrument_ring.h"

#include "protocol.h"

#ifndef PBL_ROUND

// The Emery display is 200 x 228. The dial has its origin at the top and
// angles increase clockwise. All positions are relative to the dial centre.
enum { RADIUS = 84, LABEL_HEIGHT = 22 };

typedef struct {
    char text[12];
    GRect box;
    GColor color;
    bool visible;
} WindLabel;

static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static void text_at(GContext* ctx, const char* text, GFont font, GColor color,
                    GRect box, GTextAlignment align) {
    graphics_context_set_text_color(ctx, color);
    graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static bool speed_text(char* buf, size_t size, int speed, TelemetryField field) {
    const bool fresh = state_fresh(field) && speed >= 0;
    if (fresh) snprintf(buf, size, "%d.%d", speed / SPEED_SCALE, speed % SPEED_SCALE);
    else snprintf(buf, size, "--");
    return fresh;
}

static void speed_row(GContext* ctx, GPoint center, int top, const char* label,
                      int speed, TelemetryField field) {
    char buf[16];
    const bool fresh = speed_text(buf, sizeof(buf), speed, field);
    const GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    const GFont digits = fonts_get_system_font(strlen(buf) > 4 ? FONT_KEY_GOTHIC_18_BOLD
                                                               : FONT_KEY_GOTHIC_24_BOLD);
    const int y = center.y + top;
    text_at(ctx, label, small, GColorLightGray, GRect(center.x - 46, y + 6, 34, 18), GTextAlignmentLeft);
    text_at(ctx, buf, digits, fresh ? GColorWhite : GColorLightGray,
            GRect(center.x - 16, y, 43, 28), GTextAlignmentRight);
    text_at(ctx, "kn", small, GColorLightGray, GRect(center.x + 30, y + 6, 17, 18), GTextAlignmentLeft);
}

// Reserve the central speed rows when a label sits on the lower sides of the ring.
static void clear_speed_rows(GRect* box, GPoint center, int width) {
    if (box->origin.y >= center.y + 64 || box->origin.y + box->size.h <= center.y) return;
    const int right = center.x + 48;
    const int left = center.x - 48 - box->size.w;
    if (box->origin.x + box->size.w / 2 > center.x) {
        box->origin.x = clamp(box->origin.x, right, width - box->size.w);
    } else {
        box->origin.x = clamp(box->origin.x, 0, left);
    }
}

static WindLabel wind_label(char prefix, int angle, TelemetryField field, GPoint center,
                            GFont font, int width) {
    WindLabel label = {0};
    if (angle < 0 || angle >= 360 || !state_fresh(field)) return label;
    label.visible = true;
    const int signed_angle = angle > 180 ? angle - 360 : angle;
    label.color = angle == 0 || angle == 180 ? GColorWhite :
                  signed_angle < 0 ? GColorRed : GColorGreen;
    snprintf(label.text, sizeof(label.text), "%c %d°", prefix, abs(signed_angle));

    const int trig_angle = angle * TRIG_MAX_ANGLE / 360;
    const int x = center.x + RADIUS * sin_lookup(trig_angle) / TRIG_MAX_RATIO;
    const int y = center.y - RADIUS * cos_lookup(trig_angle) / TRIG_MAX_RATIO;
    const int w = graphics_text_layout_get_content_size(label.text, font, GRect(0, 0, width, 24),
                            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter).w + 4;
    label.box = GRect(clamp(x - w / 2, 0, width - w), y - LABEL_HEIGHT / 2, w, LABEL_HEIGHT);
    clear_speed_rows(&label.box, center, width);
    return label;
}

static void separate_labels(WindLabel* a, WindLabel* t, GPoint center, GRect bounds) {
    if (!a->visible || !t->visible) return;
    GRect* first = &a->box;
    GRect* second = &t->box;
    if (first->origin.x + first->size.w + 1 <= second->origin.x ||
        second->origin.x + second->size.w + 1 <= first->origin.x ||
        abs(first->origin.y - second->origin.y) >= LABEL_HEIGHT) return;

    // At the top and bottom there is room to split along the horizontal
    // tangent. On the sides, stack along the vertical tangent.
    const int y = (first->origin.y + second->origin.y + LABEL_HEIGHT) / 2;
    if (abs(y - center.y) >= RADIUS - 9) {
        if (first->origin.x > second->origin.x) { first = &t->box; second = &a->box; }
        const int span = first->size.w + 2 + second->size.w;
        const int mid = (first->origin.x + first->size.w + second->origin.x) / 2;
        first->origin.x = clamp(mid - span / 2, 0, bounds.size.w - span);
        second->origin.x = first->origin.x + first->size.w + 2;
        first->origin.y = second->origin.y = y - LABEL_HEIGHT / 2;
    } else {
        if (first->origin.y > second->origin.y) { first = &t->box; second = &a->box; }
        first->origin.y = y - LABEL_HEIGHT;
        second->origin.y = y;
        clear_speed_rows(first, center, bounds.size.w);
        clear_speed_rows(second, center, bounds.size.w);
    }
}

static void draw_wind_label(GContext* ctx, WindLabel label, GFont font) {
    if (!label.visible) return;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, label.box, 0, GCornerNone);
    label.box.origin.y -= 2; // Gothic's glyphs sit below the text box origin.
    label.box.size.h += 2;
    text_at(ctx, label.text, font, label.color, label.box, GTextAlignmentCenter);
}

static const char* instrument_status(const AppState* state) {
    switch (state->conn) {
        case CONN_CONNECTED: break;
        case CONN_NO_CONFIG: return "SET UP ON PHONE";
        case CONN_AWAITING_APPROVAL: return "APPROVE ON SERVER";
        case CONN_ACCESS_DENIED: return "ACCESS DENIED";
        case CONN_CONNECTING: return "CONNECTING...";
        default: return "DISCONNECTED";
    }
    for (TelemetryField field = FIELD_SOG; field <= FIELD_TRUE_WIND_ANGLE; field++) {
        if (state->updated[field] && !state_fresh(field)) return "DATA STALE";
    }
    return NULL;
}

static GPoint ring_center(GRect bounds) {
    return GPoint(bounds.size.w / 2, bounds.size.h - 100);
}

GPoint instrument_ring_draw_rim(GContext* ctx, GRect bounds, const AppState* state) {
    const GPoint c = ring_center(bounds);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_circle(ctx, c, RADIUS);
    for (int quadrant = 0; quadrant < 4; quadrant++) {
        const int angle = quadrant * TRIG_MAX_ANGLE / 4;
        const int dx = sin_lookup(angle) / TRIG_MAX_RATIO;
        const int dy = -cos_lookup(angle) / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, GPoint(c.x + RADIUS * dx, c.y + RADIUS * dy),
                           GPoint(c.x + (RADIUS - 4) * dx, c.y + (RADIUS - 4) * dy));
    }

    const GFont angle_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    WindLabel apparent = wind_label('A', state->wind_angle, FIELD_WIND_ANGLE, c, angle_font, bounds.size.w);
    WindLabel true_wind = wind_label('T', state->true_wind_angle, FIELD_TRUE_WIND_ANGLE, c, angle_font, bounds.size.w);
    separate_labels(&apparent, &true_wind, c, bounds);
    draw_wind_label(ctx, apparent, angle_font);
    draw_wind_label(ctx, true_wind, angle_font);
    return c;
}

void instrument_ring_draw(GContext* ctx, GRect bounds, const AppState* state, GFont speed_font) {
    const GPoint c = ring_center(bounds);
    const GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    const char* status = instrument_status(state);
    if (status) text_at(ctx, status, small, GColorLightGray, GRect(0, 21, bounds.size.w, 18), GTextAlignmentCenter);
    char speed[16];
    const bool fresh = speed_text(speed, sizeof(speed), state->sog, FIELD_SOG);
    text_at(ctx, "SOG", small, GColorLightGray, GRect(c.x - 50, c.y - 67, 100, 18), GTextAlignmentCenter);
    text_at(ctx, speed, speed_font, fresh ? GColorWhite : GColorLightGray,
            GRect(c.x - 67, c.y - 54, 134, 44), GTextAlignmentCenter);
    text_at(ctx, "kn", small, GColorLightGray, GRect(c.x - 30, c.y - 17, 60, 18), GTextAlignmentCenter);
    speed_row(ctx, c, -4, "SOW", state->stw, FIELD_STW);
    speed_row(ctx, c, 16, "APP", state->wind_speed, FIELD_WIND_SPEED);
    speed_row(ctx, c, 36, "TRUE", state->true_wind_speed, FIELD_TRUE_WIND_SPEED);
    instrument_ring_draw_rim(ctx, bounds, state);
}
#endif
