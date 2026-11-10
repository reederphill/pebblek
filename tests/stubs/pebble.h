#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct AppTimer AppTimer;
AppTimer* app_timer_register(uint32_t ms, void (*callback)(void*), void* data);
void app_timer_cancel(AppTimer* timer);
void vibes_long_pulse(void);
