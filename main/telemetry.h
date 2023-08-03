#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

struct telemetry_cfg_t {
  /**
   * Interval between telemetry messages in seconds
   */
  uint32_t status_interval_s;

  /**
   * Interval between metrics messages in seconds
   */
  uint32_t metrics_interval_s;
};

telemetry_cfg_t telemetry_get_cfg();

esp_err_t telemetry_set_cfg(telemetry_cfg_t cfg);

void telemetry_init(EventGroupHandle_t net_group);
