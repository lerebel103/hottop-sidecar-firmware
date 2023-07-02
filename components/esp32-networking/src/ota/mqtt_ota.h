#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

esp_err_t mqtt_ota_init(EventGroupHandle_t networkEventGroup);
