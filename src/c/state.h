/*
 * Watch-side application state.
 *
 * A single AppState instance holds everything the dashboard renders: the latest
 * telemetry relayed by the phone (heading, autopilot status, SOG, wind) plus the
 * connection status. The struct is plain
 * data; behaviour lives in the modules that read/write it (comm, ui, buttons).
 */

#pragma once

#include <pebble.h>

typedef enum {
    FIELD_HEADING, FIELD_AUTOPILOT, FIELD_SOG, FIELD_STW,
    FIELD_WIND_SPEED, FIELD_WIND_ANGLE, FIELD_TRUE_WIND_SPEED, FIELD_TRUE_WIND_ANGLE,
    FIELD_TARGET_HEADING, FIELD_TARGET_WIND, FIELD_COUNT
} TelemetryField;

typedef struct {
    int heading;          // degrees 0..359
    int autopilot_state;  // ApState
    time_t updated[FIELD_COUNT]; // source timestamps, 0 = unavailable
    int stw;              // speed through water, tenths of a knot
    int true_wind_speed;  // water-referenced true wind, tenths of a knot
    int true_wind_angle;  // degrees 0..359, relative to bow
    int sog;              // tenths of a knot
    int wind_speed;       // tenths of a knot
    int wind_angle;       // degrees 0..359, apparent, 0 = bow
    int target_heading;   // degrees 0..359, or TARGET_NONE
    int target_wind;      // degrees 0..359, apparent, 0 = bow, or TARGET_NONE
    int conn;             // ConnStatus
    bool holding;         // an UP/DOWN hold is auto-repeating heading steps
    bool dial_active;     // a touch-dialled change is pending (see dial.h)
    int dial_delta;       // the pending change in degrees, + = starboard
} AppState;

// The single shared state instance.
AppState* app_state(void);
bool state_fresh(TelemetryField field);
time_t state_next_expiry(void);
void state_invalidate(void);
