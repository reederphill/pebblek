/*
 * Side-button handling: maps native single/long presses to autopilot commands.
 *   UP     single/long -> adjust heading +1 / +10
 *   DOWN   single/long -> adjust heading -1 / -10
 *   SELECT single      -> engage AUTO (or disengage if already engaged)
 *   SELECT long        -> engage WIND (or disengage if already engaged)
 *
 * Each press sends a command via comm and shows optimistic feedback via ui.
 */

#pragma once

// Subscribe the button handlers; pass to window_set_click_config_provider().
void buttons_click_config_provider(void* context);
