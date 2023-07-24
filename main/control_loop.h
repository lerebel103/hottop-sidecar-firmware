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
  uint32_t tc_read_error_count;

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

void control_loop_run();

void control_loop_stop();

control_state_t controller_get_state();

void control_loop_init(EventGroupHandle_t net_group);