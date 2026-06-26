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

void comm_send_int(uint32_t key, int value) {
    DictionaryIterator* iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
    dict_write_int(iter, key, &value, sizeof(value), true);
    app_message_outbox_send();
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

    if ((t = dict_find(iter, MESSAGE_KEY_CMD_RESULT))) {
        bool ok = (read_int(t) == CMD_RESULT_OK);
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
}

static void outbox_sent(DictionaryIterator* iter, void* context) {}

void comm_init(void) {
    app_message_register_inbox_received(inbox_received);
    app_message_register_inbox_dropped(inbox_dropped);
    app_message_register_outbox_failed(outbox_failed);
    app_message_register_outbox_sent(outbox_sent);
    app_message_open(256, 64);
}
