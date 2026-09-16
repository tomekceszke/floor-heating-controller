#pragma once

#include <stdbool.h>

void notify_init(void);
void notify_queue_error(const char *msg);
void notify_error_suppress(bool suppress);

void notify_pump_started(float temp);
void notify_pump_stopped(float temp);
void notify_device_started(void);
void notify_device_ready(float temp, bool pump_running);
void notify_maintenance_started(void);
