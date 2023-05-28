#pragma once


#include <freertos/event_groups.h>

const char* mqtt_thing_id();

esp_err_t mqtt_init(EventGroupHandle_t networkEventGroup);
