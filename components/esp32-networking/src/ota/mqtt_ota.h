#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

int mqtt_ota_init(EventGroupHandle_t networkEventGroup);
