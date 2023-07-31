#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

struct control_state_t {
  // loop count
  uint32_t loop_count;

  // Temperature of thermocouple
  float tc_temp;

  // Internal or junction temperature
  float junction_temp;

  // Thermocouple status
  uint8_t tc_status;

  // Temperature read error count
  uint32_t tc_error_count;

  // Motor on/off
  bool motor_on;

  // Fan duty
  uint8_t fan_duty;

  // Balance
  double balance;

  // Input duty
  uint8_t input_duty;

  // Output duty
  uint8_t output_duty;
};

struct control_cfg_t {
  /**
   * Maximum allowed ratio of input to output heat duty.
   *
   * The controller will not allow the output duty to exceed input*ratio. This ratio
   * can be controlled by the user via a potentionmeter.
   */
  float max_heat_ratio;

  /**
   * Safety limit to cut off the secondary heat element when the thermocouple exceeds this threshold
   */
  uint16_t max_tc_temp;

  /**
   * Safety limit to cut off everything when internal board temp is exceeded.
   */
  uint8_t max_board_temp;

  /**
   * 50Hz or 60Hz mains frequency
   */
  uint8_t mains_hz;
};

void control_loop_run();

void control_loop_stop();

control_state_t controller_get_state();

/**
 * Retrieves the current configuration.
 * @return
 */
control_cfg_t controller_get_cfg();

/**
 * Sets the new configuration.
 * @param cfg New configuration to set
 * @return ESP_OK if the configuration was set successfully, ESP_FAIL otherwise where values were not in compliance.
 */
esp_err_t controller_set_cfg(control_cfg_t cfg);

void control_loop_init(EventGroupHandle_t net_group);