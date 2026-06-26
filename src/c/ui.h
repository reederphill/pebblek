/*
 * On-watch UI: the window, its single rendering layer, and the dashboard /
 * connection-status drawing. Also owns the transient feedback pill (optimistic
 * button presses and command acks) and its expiry timer.
 *
 * Rendering reads from app_state(); other modules push updates in via
 * ui_set_feedback() and ui_mark_dirty().
 */

#pragma once

// Create and push the window. Call once at startup.
void ui_init(void);

// Tear down the window and release UI resources.
void ui_deinit(void);

// Request a redraw of the dashboard.
void ui_mark_dirty(void);

// Show a short transient message in the feedback pill; auto-clears after a few
// seconds. Passing the empty string clears it immediately on the next draw.
void ui_set_feedback(const char* label);
