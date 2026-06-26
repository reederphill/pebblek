/*
 * pebblek — native C watch app (SignalK boat monitor + autopilot remote).
 *
 * The phone side (src/pkjs) is unchanged PebbleKit JS: it talks to the SignalK
 * server and relays telemetry to the watch over AppMessage, and turns our button
 * commands into autopilot REST calls. This file is just the on-watch UI:
 *   - receives telemetry AppMessages -> updates state -> redraws
 *   - draws the dashboard (heading, autopilot status, SOG, wind) or a connection
 *     status screen, on both emery (rectangular) and gabbro (round)
 *   - maps the side buttons to autopilot commands (native single/long press)
 *
 * The AppMessage value encoding is shared with the phone via protocol.h /
 * src/common/protocol.js. Message keys come from package.json -> messageKeys as
 * MESSAGE_KEY_<NAME>.
 */

#include <pebble.h>
#include "protocol.h"

typedef struct {
  int heading;          // degrees 0..359
  int autopilot_state;  // ApState
  int sog;              // tenths of a knot
  int wind_speed;       // tenths of a knot
  int wind_angle;       // degrees 0..359, apparent, 0 = bow
  int conn;             // ConnStatus
  char feedback[16];    // transient button/ack feedback, empty when none
} AppState;

static Window *s_window;
static Layer *s_canvas;
static GPath *s_arrow;
static AppTimer *s_feedback_timer;
static AppState s_state;

static const GPathInfo ARROW_POINTS = {
  .num_points = 4,
  .points = (GPoint[]) {{0, -16}, {6, 6}, {0, 1}, {-6, 6}},
};

// ---- helpers --------------------------------------------------------------

// Read an AppMessage integer tuple regardless of the width the phone packed it
// into (PebbleKit JS picks the smallest signed/unsigned int that fits).
static int32_t read_int(const Tuple *t) {
  switch (t->type) {
    case TUPLE_INT:
      switch (t->length) {
        case 1: return t->value->int8;
        case 2: return t->value->int16;
        case 4: return t->value->int32;
      }
      break;
    case TUPLE_UINT:
      switch (t->length) {
        case 1: return t->value->uint8;
        case 2: return t->value->uint16;
        case 4: return t->value->uint32;
      }
      break;
    default:
      break;
  }
  return 0;
}

static const char *conn_text(int conn) {
  switch (conn) {
    case CONN_NO_CONFIG:         return "Open pebblek\nsettings on\nyour phone";
    case CONN_CONNECTING:        return "Connecting...";
    case CONN_AWAITING_APPROVAL: return "Waiting for\napproval on\nSignalK server";
    case CONN_ACCESS_DENIED:     return "Access denied";
    case CONN_DISCONNECTED:
    default:                     return "Disconnected\nReconnecting...";
  }
}

// ---- feedback (optimistic button + command ack) ---------------------------

static void feedback_timeout(void *data) {
  s_feedback_timer = NULL;
  s_state.feedback[0] = '\0';
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void set_feedback(const char *label) {
  strncpy(s_state.feedback, label, sizeof(s_state.feedback) - 1);
  s_state.feedback[sizeof(s_state.feedback) - 1] = '\0';
  if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
  s_feedback_timer = app_timer_register(4000, feedback_timeout, NULL);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

// ---- outbound commands ----------------------------------------------------

static void send_int(uint32_t key, int value) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_int(iter, key, &value, sizeof(value), true);
  app_message_outbox_send();
}

// ---- AppMessage inbound ---------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_HEADING)))           s_state.heading = read_int(t);
  if ((t = dict_find(iter, MESSAGE_KEY_AUTOPILOT_STATE)))   s_state.autopilot_state = read_int(t);
  if ((t = dict_find(iter, MESSAGE_KEY_SOG)))               s_state.sog = read_int(t);
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED)))        s_state.wind_speed = read_int(t);
  if ((t = dict_find(iter, MESSAGE_KEY_WIND_ANGLE)))        s_state.wind_angle = read_int(t);
  if ((t = dict_find(iter, MESSAGE_KEY_CONNECTION_STATUS))) s_state.conn = read_int(t);

  if ((t = dict_find(iter, MESSAGE_KEY_CMD_RESULT))) {
    bool ok = (read_int(t) == CMD_RESULT_OK);
    set_feedback(ok ? "OK" : "FAILED");
    // Haptic confirmation: a short pulse on success, a long pulse on failure.
    if (ok) {
      vibes_short_pulse();
    } else {
      vibes_long_pulse();
    }
  }

  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", reason);
}

static void outbox_sent(DictionaryIterator *iter, void *context) {}

// ---- buttons --------------------------------------------------------------

static void up_click(ClickRecognizerRef r, void *c)     { send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, 1);   set_feedback("+1°"); }
static void up_long(ClickRecognizerRef r, void *c)      { send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, 10);  set_feedback("+10°"); }
static void down_click(ClickRecognizerRef r, void *c)   { send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, -1);  set_feedback("-1°"); }
static void down_long(ClickRecognizerRef r, void *c)    { send_int(MESSAGE_KEY_CMD_ADJUST_HEADING, -10); set_feedback("-10°"); }
static void select_click(ClickRecognizerRef r, void *c) { send_int(MESSAGE_KEY_CMD_TOGGLE_ENGAGE, 1);    set_feedback("ENGAGE"); }

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_long_click_subscribe(BUTTON_ID_UP, 0, up_long, NULL);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_long_click_subscribe(BUTTON_ID_DOWN, 0, down_long, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

// ---- rendering ------------------------------------------------------------

static void draw_row(GContext *ctx, const char *text, GFont font, int y, int h, int w) {
  graphics_draw_text(ctx, text, font, GRect(0, y, w, h),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const int w = b.size.w;
  char buf[24];

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, GColorWhite);

  GFont f_big   = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  GFont f_med   = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont f_small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  GFont f_tiny  = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Connection status screen replaces the dashboard until connected.
  if (s_state.conn != CONN_CONNECTED) {
    draw_row(ctx, conn_text(s_state.conn), f_med, b.size.h / 2 - 52, 120, w);
    return;
  }

  const int top = PBL_IF_ROUND_ELSE(20, 6);

  draw_row(ctx, "HEADING", f_tiny, top, 16, w);
  snprintf(buf, sizeof(buf), "%d°", s_state.heading);
  draw_row(ctx, buf, f_big, top + 13, 46, w);

  const bool engaged = (s_state.autopilot_state != AP_STATE_STANDBY &&
                        s_state.autopilot_state != AP_STATE_UNKNOWN);
  graphics_context_set_text_color(ctx, engaged ? GColorGreen : GColorLightGray);
  draw_row(ctx, engaged ? "AUTOPILOT ON" : "AUTOPILOT OFF", f_med, top + 60, 26, w);
  graphics_context_set_text_color(ctx, GColorWhite);

  snprintf(buf, sizeof(buf), "SOG %d.%d kn", s_state.sog / 10, s_state.sog % 10);
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
  gpath_rotate_to(s_arrow, DEG_TO_TRIGANGLE(s_state.wind_angle));
  gpath_move_to(s_arrow, center);
  graphics_context_set_fill_color(ctx, GColorRed);
  gpath_draw_filled(ctx, s_arrow);

  snprintf(buf, sizeof(buf), "%d.%d", s_state.wind_speed / 10, s_state.wind_speed % 10);
  graphics_draw_text(ctx, buf, f_med, GRect(w / 2, wind_y, w / 2 - 6, 28),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "WIND kn", f_tiny, GRect(w / 2, wind_y + 28, w / 2 - 6, 16),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Transient feedback pill (button pressed / command acked).
  if (s_state.feedback[0]) {
    const GRect pill = GRect(w / 2 - 44, b.size.h - PBL_IF_ROUND_ELSE(34, 26), 88, 24);
    graphics_context_set_fill_color(ctx, GColorBlue);
    graphics_fill_rect(ctx, pill, 6, GCornersAll);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_state.feedback, f_small, GRect(pill.origin.x, pill.origin.y + 1, pill.size.w, pill.size.h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ---- window / app lifecycle ----------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  s_state.conn = CONN_DISCONNECTED;
  s_state.autopilot_state = AP_STATE_UNKNOWN;
  s_arrow = gpath_create(&ARROW_POINTS);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(256, 64);
}

static void deinit(void) {
  if (s_feedback_timer) app_timer_cancel(s_feedback_timer);
  window_destroy(s_window);
  gpath_destroy(s_arrow);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
