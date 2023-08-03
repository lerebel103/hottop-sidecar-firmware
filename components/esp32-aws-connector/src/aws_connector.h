#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>


void aws_connector_init(EventGroupHandle_t net_group);
