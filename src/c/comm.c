#include "comm.h"

#include "protocol.h"
#include "state.h"
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
static int s_pending_adjust;              // degrees, 0 = nothing queued
static int s_pending_state = -1;          // ApState, -1 = nothing queued

static void write_int(DictionaryIterator* iter, uint32_t key, int value) {
    dict_write_int(iter, key, &value, sizeof(value), true);
}

static void flush(void) {
    if (s_in_flight || (s_pending_adjust == 0 && s_pending_state < 0)) return;
    DictionaryIterator* iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
    // Heading deltas go first; a queued state change follows in the next message
    // so the phone applies them in the order the user pressed them.
    if (s_pending_adjust != 0) {
        write_int(iter, MESSAGE_KEY_CMD_ADJUST_HEADING, s_pending_adjust);
        s_pending_adjust = 0;
    } else {
        write_int(iter, MESSAGE_KEY_CMD_SET_STATE, s_pending_state);
        s_pending_state = -1;
    }
    s_in_flight = (app_message_outbox_send() == APP_MSG_OK);
}

void comm_adjust_heading(int delta) {
    s_pending_adjust += delta;
    flush();
}

void comm_set_state(int ap_state) {
    s_pending_state = ap_state;
    flush();
}

// ---- AppMessage callbacks -------------------------------------------------

static void inbox_received(DictionaryIterator* iter, void* context) {
    AppState* state = app_state();
    Tuple* t;
    if ((t = dict_find(iter, MESSAGE_KEY_HEADING))) state->heading = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_AUTOPILOT_STATE))) state->autopilot_state = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_SOG))) state->sog = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_WIND_SPEED))) state->wind_speed = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_WIND_ANGLE))) state->wind_angle = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_CONNECTION_STATUS))) state->conn = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_TARGET_HEADING))) state->target_heading = read_int(t);
    if ((t = dict_find(iter, MESSAGE_KEY_TARGET_WIND_ANGLE))) state->target_wind = read_int(t);

    if ((t = dict_find(iter, MESSAGE_KEY_CMD_RESULT))) {
        bool ok = (read_int(t) == CMD_RESULT_OK);
        // While a button is held the pill shows the running total and each step
        // already ticks, so don't interrupt it with per-step OK acks.
        if (ok && state->holding) {
            ui_mark_dirty();
            return;
        }
        ui_set_feedback(ok ? "OK" : "FAILED");
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
    ui_set_feedback("NOT SENT");
    vibes_long_pulse();
    flush();
}

static void outbox_sent(DictionaryIterator* iter, void* context) {
    s_in_flight = false;
    flush();
}

void comm_init(void) {
    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_register_outbox_failed(outbox_failed);
    app_message_register_outbox_sent(outbox_sent);
    app_message_open(256, 64);
}
