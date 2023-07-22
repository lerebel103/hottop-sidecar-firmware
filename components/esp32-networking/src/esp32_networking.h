#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>


void esp32_networking_init(EventGroupHandle_t net_group);
