#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

void telemetry_init(EventGroupHandle_t net_group);
