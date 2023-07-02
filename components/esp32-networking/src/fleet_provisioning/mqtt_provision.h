#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

bool mqtt_provisioning_active();

esp_err_t mqtt_provision_init(EventGroupHandle_t networkEventGroup);
