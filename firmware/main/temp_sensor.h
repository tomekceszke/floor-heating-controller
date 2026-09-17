#pragma once

#include <stdbool.h>
#include <stdint.h>

/* DS18B20 on the 1-Wire bus. Blocks for one conversion (~750 ms); call from the control task only.
 * Rescans the bus while no sensor is known. False on any failure, including the 85.0 °C power-on value. */
bool temp_sensor_read(int16_t *temp_x10);
