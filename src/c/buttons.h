/*
 * Pilot-page buttons: maps native single/long presses to autopilot commands.
 *   UP     single -> adjust heading +1
 *   UP     hold   -> +10, repeating every 0.4s until released
 *   DOWN   single -> adjust heading -1
 *   DOWN   hold   -> -10, repeating every 0.4s until released
 *   SELECT single      -> engage AUTO (or disengage if already engaged)
 *   SELECT long        -> engage WIND (or disengage if already engaged)
 *
 * BACK cancels unsent adjustments and returns to the instrument home.
 * Home has a separate click provider; only UP opens this page.
 */

#pragma once

// Subscribe the button handlers; pass to window_set_click_config_provider().
void buttons_click_config_provider(void* context);
// Stop a held-button repeat when leaving the pilot or losing fresh status.
void buttons_cancel(void);
