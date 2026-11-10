#pragma once

#include <pebble.h>

// Signal K notification severities, in wire order. Level zero clears an alert.
typedef enum {
    ALERT_CLEAR, ALERT_ADVISORY, ALERT_WARNING, ALERT_ALARM, ALERT_EMERGENCY,
} AlertLevel;

void alerts_apply(int id, int level, const char* text, int notify);
void alerts_sync(int sync);
void alerts_connection_changed(void);
void alerts_open(ClickRecognizerRef recognizer, void* context);
void alerts_init(void);
void alerts_deinit(void);
