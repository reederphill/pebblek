/*
 * Watch-side application state.
 *
 * A single AppState instance holds everything the dashboard renders: the latest
 * telemetry relayed by the phone (heading, autopilot status, SOG, wind) plus the
 * connection status and any transient on-screen feedback. The struct is plain
 * data; behaviour lives in the modules that read/write it (comm, ui, buttons).
 */

#pragma once

#include <pebble.h>

typedef struct {
    int heading;          // degrees 0..359
    int autopilot_state;  // ApState
    int sog;              // tenths of a knot
    int wind_speed;       // tenths of a knot
    int wind_angle;       // degrees 0..359, apparent, 0 = bow
    int conn;             // ConnStatus
    char feedback[16];    // transient button/ack feedback, empty when none
} AppState;

// The single shared state instance.
AppState* app_state(void);
