#pragma once


#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

const char* mqtt_thing_id();

bool mqtt_is_provisioning();

esp_err_t mqtt_init(EventGroupHandle_t networkEventGroup);
