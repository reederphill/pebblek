#pragma once

#include "state.h"

// Draw the shared rectangular wind ring and return its centre.
GPoint instrument_ring_draw_rim(GContext* ctx, GRect bounds, const AppState* state);

// Draw the rectangular instrument home below the clock/pilot header.
void instrument_ring_draw(GContext* ctx, GRect bounds, const AppState* state, GFont speed_font);
