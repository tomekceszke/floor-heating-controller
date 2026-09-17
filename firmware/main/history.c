#include "freertos/FreeRTOS.h"

#include "config/config.h"
#include "events.h"
#include "history.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static int16_t s_temps[HISTORY_SAMPLES];
static uint8_t s_pump[(HISTORY_SAMPLES + 7) / 8];
static size_t s_head;
static size_t s_count;
static int64_t s_newest_mono_s;

void history_add(bool valid, int16_t temp_x10, bool pump_on)
{
    int64_t now = events_mono_s();
    portENTER_CRITICAL(&s_mux);
    s_temps[s_head] = valid ? temp_x10 : HISTORY_NO_READING;
    if (pump_on) s_pump[s_head / 8] |= (uint8_t) (1u << (s_head % 8));
    else s_pump[s_head / 8] &= (uint8_t) ~(1u << (s_head % 8));
    s_head = (s_head + 1) % HISTORY_SAMPLES;
    if (s_count < HISTORY_SAMPLES) s_count++;
    s_newest_mono_s = now;
    portEXIT_CRITICAL(&s_mux);
}

size_t history_copy(int16_t *temps, bool *pump, size_t max, int64_t *newest_mono_s)
{
    portENTER_CRITICAL(&s_mux);
    size_t n = s_count < max ? s_count : max;
    size_t start = (s_head + HISTORY_SAMPLES - n) % HISTORY_SAMPLES;
    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % HISTORY_SAMPLES;
        temps[i] = s_temps[idx];
        pump[i] = (s_pump[idx / 8] >> (idx % 8)) & 1u;
    }
    *newest_mono_s = s_newest_mono_s;
    portEXIT_CRITICAL(&s_mux);
    return n;
}
