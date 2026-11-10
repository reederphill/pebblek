#include "comm.h"

#include "alerts.h"
#include "buttons.h"
#include "dial.h"
#include "protocol.h"
#include "state.h"
#include "target.h"
#include "ui.h"

// ---- helpers --------------------------------------------------------------

// Read an AppMessage integer tuple regardless of the width the phone packed it
// into (PebbleKit JS picks the smallest signed/unsigned int that fits).
static int32_t read_int(const Tuple* t) {
    switch (t->type) {
        case TUPLE_INT:
            switch (t->length) {
                case 1:
                    return t->value->int8;
                case 2:
                    return t->value->int16;
                case 4:
                    return t->value->int32;
            }
            break;
        case TUPLE_UINT:
            switch (t->length) {
                case 1:
                    return t->value->uint8;
                case 2:
                    return t->value->uint16;
                case 4:
                    return t->value->uint32;
            }
            break;
        default:
            break;
    }
    return 0;
}

// ---- outbound commands ----------------------------------------------------
//
// AppMessage allows one outbound message in flight. Commands are queued here and
// sent as soon as the outbox is free: heading deltas accumulate into a single
// sum, and the latest requested state wins.

static bool s_in_flight;
static bool s_seen_alerts;
static bool s_need_resync;
static AppTimer* s_resync_retry;
static int s_pending_adjust;              // degrees, 0 = nothing queued
static int s_pending_state = -1;          // ApState, -1 = nothing queued

void comm_cancel_pending(void) {
    s_pending_adjust = 0;
    s_pending_state = -1;
}

static void write_int(DictionaryIterator* iter, uint32_t key, int value) {
    dict_write_int(iter, key, &value, sizeof(value), true);
}

static bool can_adjust(void) {
    const int ap = app_state()->autopilot_state;
    return (ap == AP_STATE_AUTO && state_fresh(FIELD_TARGET_HEADING)) ||
           (ap == AP_STATE_WIND && state_fresh(FIELD_TARGET_WIND));
}

static void flush(void);

static void retry_resync(void* unused) {
    s_resync_retry = NULL;
    flush();
}

static void schedule_resync_retry(void) {
    if (s_need_resync && !s_resync_retry && connection_service_peek_pebble_app_connection()) {
        s_resync_retry = app_timer_register(2000, retry_resync, NULL);
    }
}

static void flush(void) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) {
        comm_cancel_pending();
    }
    if (!can_adjust()) s_pending_adjust = 0;
    if (s_in_flight || (s_pending_adjust == 0 && s_pending_state < 0 && !s_need_resync)) return;
    DictionaryIterator* iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
        schedule_resync_retry();
        return;
    }
    // Heading deltas go first; a queued state change follows in the next message
    // so the phone applies them in the order the user pressed them.
    const int adjustment = s_pending_adjust;
    if (adjustment != 0) {
        write_int(iter, MESSAGE_KEY_CMD_ADJUST_HEADING, s_pending_adjust);
        s_pending_adjust = 0;
    } else if (s_pending_state >= 0) {
        write_int(iter, MESSAGE_KEY_CMD_SET_STATE, s_pending_state);
        s_pending_state = -1;
    } else {
        write_int(iter, MESSAGE_KEY_ALERT_RESYNC, 1);
    }
    s_in_flight = (app_message_outbox_send() == APP_MSG_OK);
    if (s_in_flight && adjustment) target_on_adjust_sent(adjustment);
    if (!s_in_flight) schedule_resync_retry();
}

void comm_adjust_heading(int delta) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT) || !can_adjust()) return;
    s_pending_adjust += delta;
    flush();
}

void comm_set_state(int ap_state) {
    if (!ui_is_pilot() || !state_fresh(FIELD_AUTOPILOT)) return;
    s_pending_state = ap_state;
    flush();
}

// ---- AppMessage callbacks -------------------------------------------------

static AppTimer* s_freshness_timer;
static void schedule_expiry(void);

static void expire_readings(void* context) {
    s_freshness_timer = NULL;
    if (!state_fresh(FIELD_AUTOPILOT)) {
        buttons_cancel();
        comm_cancel_pending();
    }
    if (!can_adjust()) s_pending_adjust = 0;
    dial_is_active();
    ui_mark_dirty();
    schedule_expiry();
}

// Sleep until the earliest live reading expires. No continuous redraw/poll loop.
static void schedule_expiry(void) {
    if (s_freshness_timer) app_timer_cancel(s_freshness_timer);
    s_freshness_timer = NULL;
    const time_t next = state_next_expiry();
    if (next) {
        const int seconds = next - time(NULL);
        s_freshness_timer = app_timer_register(seconds > 0 ? seconds * 1000 : 1,
                                              expire_readings, NULL);
    }
}

static void receive_reading(DictionaryIterator* iter, uint32_t key, uint32_t time_key,
                            TelemetryField field, int* dest) {
    Tuple* value = dict_find(iter, key);
    Tuple* stamp = dict_find(iter, time_key);
    if (!value || !stamp) return;
    *dest = read_int(value);
    app_state()->updated[field] = read_int(stamp);
}

static void receive_target(DictionaryIterator* iter, uint32_t key, uint32_t time_key,
                           TelemetryField field, int mode) {
    Tuple* value = dict_find(iter, key);
    Tuple* stamp = dict_find(iter, time_key);
    if (!value || !stamp) return;
    app_state()->updated[field] = read_int(stamp);
    target_on_server(mode, state_fresh(field) ? read_int(value) : TARGET_NONE);
}

static void inbox_received(DictionaryIterator* iter, void* context) {
    AppState* state = app_state();
    Tuple* t;
    if ((t = dict_find(iter, MESSAGE_KEY_CONNECTION_STATUS))) {
        const int conn = read_int(t);
        if (state->conn != conn) {
            target_on_mode(AP_STATE_UNKNOWN);
            state_invalidate();
            state->conn = conn;
            alerts_connection_changed();
        }
    }
    if ((t = dict_find(iter, MESSAGE_KEY_ALERT_SYNC))) {
        s_seen_alerts = true;
        alerts_sync(read_int(t));
    }
    Tuple* alert_id = dict_find(iter, MESSAGE_KEY_ALERT_ID);
    Tuple* level = dict_find(iter, MESSAGE_KEY_ALERT_LEVEL);
    if (alert_id && level) {
        Tuple* message = dict_find(iter, MESSAGE_KEY_ALERT_TEXT);
        Tuple* notify = dict_find(iter, MESSAGE_KEY_ALERT_NOTIFY);
        alerts_apply(read_int(alert_id), read_int(level),
                     message && message->type == TUPLE_CSTRING ? message->value->cstring : NULL,
                     notify ? read_int(notify) : 0);
    }
    receive_reading(iter, MESSAGE_KEY_HEADING, MESSAGE_KEY_HEADING_TIME,
                    FIELD_HEADING, &state->heading);
    receive_reading(iter, MESSAGE_KEY_AUTOPILOT_STATE, MESSAGE_KEY_AUTOPILOT_STATE_TIME,
                    FIELD_AUTOPILOT, &state->autopilot_state);
    receive_reading(iter, MESSAGE_KEY_SOG, MESSAGE_KEY_SOG_TIME, FIELD_SOG, &state->sog);
    receive_reading(iter, MESSAGE_KEY_STW, MESSAGE_KEY_STW_TIME, FIELD_STW, &state->stw);
    receive_reading(iter, MESSAGE_KEY_WIND_SPEED, MESSAGE_KEY_WIND_SPEED_TIME,
                    FIELD_WIND_SPEED, &state->wind_speed);
    receive_reading(iter, MESSAGE_KEY_WIND_ANGLE, MESSAGE_KEY_WIND_ANGLE_TIME,
                    FIELD_WIND_ANGLE, &state->wind_angle);
    receive_reading(iter, MESSAGE_KEY_TRUE_WIND_SPEED, MESSAGE_KEY_TRUE_WIND_SPEED_TIME,
                    FIELD_TRUE_WIND_SPEED, &state->true_wind_speed);
    receive_reading(iter, MESSAGE_KEY_TRUE_WIND_ANGLE, MESSAGE_KEY_TRUE_WIND_ANGLE_TIME,
                    FIELD_TRUE_WIND_ANGLE, &state->true_wind_angle);
    target_on_mode(state_fresh(FIELD_AUTOPILOT) ? state->autopilot_state : AP_STATE_UNKNOWN);
    receive_target(iter, MESSAGE_KEY_TARGET_HEADING, MESSAGE_KEY_TARGET_HEADING_TIME,
                   FIELD_TARGET_HEADING, AP_STATE_AUTO);
    receive_target(iter, MESSAGE_KEY_TARGET_WIND_ANGLE, MESSAGE_KEY_TARGET_WIND_ANGLE_TIME,
                   FIELD_TARGET_WIND, AP_STATE_WIND);
    dial_is_active();  // drops a pending dial if the autopilot was disengaged
    if (!state_fresh(FIELD_AUTOPILOT)) {
        buttons_cancel();
        comm_cancel_pending();
    }
    if (!can_adjust()) s_pending_adjust = 0;
    schedule_expiry();

    if ((t = dict_find(iter, MESSAGE_KEY_CMD_RESULT))) {
        bool ok = (read_int(t) == CMD_RESULT_OK);
        if (!ok && target_on_failure()) return;
        // While a button is held each step already ticks, so don't interrupt it
        // with per-step OK acks.
        if (ok && state->holding) {
            ui_mark_dirty();
            return;
        }
        // Haptic confirmation: a short pulse on success, a long pulse on failure.
        if (ok) {
            vibes_short_pulse();
        } else {
            vibes_long_pulse();
        }
    }

    ui_mark_dirty();
}

static void inbox_dropped(AppMessageResult reason, void* context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", reason);
}

static void outbox_failed(DictionaryIterator* iter, AppMessageResult reason, void* context) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", reason);
    s_in_flight = false;
    if (dict_find(iter, MESSAGE_KEY_ALERT_RESYNC)) {
        schedule_resync_retry();
    } else {
        if (!target_on_failure()) {
            ui_set_feedback("NOT SENT");
            vibes_long_pulse();
        }
        flush();
    }
}

static void outbox_sent(DictionaryIterator* iter, void* context) {
    if (dict_find(iter, MESSAGE_KEY_ALERT_RESYNC)) s_need_resync = false;
    s_in_flight = false;
    flush();
}

static void phone_connection_changed(bool connected) {
    alerts_connection_changed();
    s_need_resync = connected && s_seen_alerts;
    if (s_need_resync) flush();
}

void comm_init(void) {
    connection_service_subscribe((ConnectionHandlers){
        .pebble_app_connection_handler = phone_connection_changed});
    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_register_outbox_failed(outbox_failed);
    app_message_register_outbox_sent(outbox_sent);
    app_message_open(512, 64);
}

void comm_deinit(void) {
    connection_service_unsubscribe();
    if (s_resync_retry) app_timer_cancel(s_resync_retry);
    if (s_freshness_timer) app_timer_cancel(s_freshness_timer);
    target_on_mode(AP_STATE_UNKNOWN);
    app_message_deregister_callbacks();
}
