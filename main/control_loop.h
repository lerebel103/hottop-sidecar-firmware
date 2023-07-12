#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

void control_loop_run();

void control_loop_stop();

void control_loop_init(EventGroupHandle_t net_group);