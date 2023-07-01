#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

void wifi_connect_init(EventGroupHandle_t networkEventGroup);