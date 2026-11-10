/*
 * On-watch UI: the window, its single rendering layer, and the dashboard /
 * connection-status drawing.
 *
 * Rendering reads from app_state(); other modules push updates in via
 * ui_mark_dirty().
 */

#pragma once

#include <stdbool.h>

// Create and push the window. Call once at startup.
void ui_init(void);

// Tear down the window and release UI resources.
void ui_deinit(void);

// Request a redraw of the dashboard.
void ui_mark_dirty(void);
void ui_set_feedback(const char* label);
bool ui_is_pilot(void);
