#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gptimer.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/semphr.h>
#include <esp_event.h>
#include <esp_check.h>
#include <nvs.h>
#include "control_loop.h"
#include "ssr_ctrl.h"
#include "max31850.h"
#include "level_shifter.h"
#include "panel_inputs.h"
#include "balancer.h"
#include "input_pwm_duty.h"
#include "digital_input.h"
#include "reset_button.h"
#include "pm_control.h"
#include "events.h"
#include "telemetry.h"
#include "app_metrics.h"
#include "app_config.h"
#include "utils.h"

// Interval in MHz
#define INTERVAL 1000000
#define TAG "control"

#define ONEWIRE_PIN                 GPIO_NUM_2
#define HEAT_SIGNAL_PIN             GPIO_NUM_6
#define DRUM_MOTOR_SIGNAL_PIN       GPIO_NUM_7
#define FAN_SIGNAL_PIN              GPIO_NUM_9
#define SSR1_PIN                    GPIO_NUM_10
#define SSR2_PIN                    GPIO_NUM_11

#define DEFAULT_MAX_SECONDARY_HEAT_RATIO    0.7f
#define DEFAULT_TEMPERATURE_TC_MAX          280
#define DEFAULT_TEMPERATURE_BOARD_MAX       75
#define DEFAULT_MAINS_HZ                    MAINS_50_HZ

static SemaphoreHandle_t semaphoreHandle;
static gptimer_handle_t gptimer;
static bool _go = false;

static input_pwm_handle_t s_heat_pwm_in;
static input_pwm_handle_t s_fan_pwm_in;
static ssr_ctrl_handle_t s_ssr1 = nullptr;
static ssr_ctrl_handle_t s_ssr2 = nullptr;
static uint64_t s_max31850_addr = 0;

// State object that will record internal variables
static control_state_t s_state = {};

// Configuration object
static control_cfg_t s_cfg = {
    .max_heat_ratio  = DEFAULT_MAX_SECONDARY_HEAT_RATIO,
    .max_tc_temp = DEFAULT_TEMPERATURE_TC_MAX,
    .max_board_temp = DEFAULT_TEMPERATURE_BOARD_MAX,
    .mains_hz = DEFAULT_MAINS_HZ,
};

/*
 * Checks that the TC and environmental temperatures are within acceptable range
 */
static bool _check_tc(max31850_data_t elm_temp) {
  bool is_ok = false;

  // Get TC value
  if (elm_temp.is_valid) {
    if (elm_temp.thermocouple_status == MAX31850_TC_STATUS_OK) {
      ESP_LOGI(TAG, "Thermocouple=%.2fC, Board=%.2fC", elm_temp.tc_temp, elm_temp.junction_temp);
      s_state.tc_status = 0;
      s_state.tc_temp = elm_temp.tc_temp;
      s_state.junction_temp = elm_temp.junction_temp;
    } else {
      if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_OPEN_CIRCUIT) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault OPEN CIRCUIT");
        s_state.tc_error_count++;
        s_state.tc_status = MAX31850_TC_STATUS_OPEN_CIRCUIT;
      } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_GND) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO GROUND");
        s_state.tc_error_count++;
        s_state.tc_status = MAX31850_TC_STATUS_SHORT_GND;
      } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_VCC) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO VCC");
        s_state.tc_error_count++;
        s_state.tc_status = MAX31850_TC_STATUS_SHORT_VCC;
      }
    }
  } else {
    ESP_LOGE(TAG, "Unable to run, error reading thermocouple data.");
    s_state.tc_error_count++;
    s_state.tc_status = 255;
  }

  return is_ok;
}


static IRAM_ATTR bool _on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(semaphoreHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  return true;
}

/* Do it, one loop iteration */
static esp_err_t _control() {
  max31850_data_t elm_temp{};
  esp_err_t ret = ESP_OK;
  bool tc_ok;
  s_state.loop_count++;

  // Read our peripherals to figure out what to do
  ESP_GOTO_ON_ERROR(
      input_pwm_get_duty(s_heat_pwm_in, s_state.input_duty), heat_off, TAG, "Can't read input duty");
  ESP_GOTO_ON_ERROR(
      input_pwm_get_duty(s_fan_pwm_in, s_state.fan_duty), heat_off, TAG, "Can't read fan duty");
  s_state.balance = balance_read_percent();
  s_state.motor_on = digital_input_is_on(DRUM_MOTOR_SIGNAL_PIN);
  s_state.input_duty = (s_state.motor_on) ? s_state.input_duty : 0;

  // Grab temperatures and decide if it is safe to operate
  elm_temp = max31850_read(ONEWIRE_PIN, s_max31850_addr);
  tc_ok = _check_tc(elm_temp);
  if (!elm_temp.is_valid) {
    goto heat_off;
  } else if (elm_temp.is_valid && elm_temp.junction_temp > s_cfg.max_board_temp) {
    ESP_LOGW(TAG, "Board temperature exceeded: Board=%.2f, Max=%d", elm_temp.junction_temp, s_cfg.max_board_temp);
    goto heat_off;
  } else if (tc_ok && elm_temp.tc_temp < s_cfg.max_tc_temp) {
    s_state.output_duty = (uint8_t) (s_state.input_duty * s_state.balance / 100.0 * s_cfg.max_heat_ratio);
  } else {
    ESP_LOGW(TAG, "Safety not met, turning off secondary element");
    s_state.output_duty = 0;
  }

  ssr_ctrl_set_duty(s_ssr1, s_state.input_duty);
  ssr_ctrl_set_duty(s_ssr2, s_state.output_duty);

  ESP_LOGI(TAG, "Input=%d, Output=%d, Fan=%d, Balance=%f, TC=%.2f, Board=%.2f, TC Status=%d, TC Errors=%lu",
           s_state.input_duty, s_state.output_duty, s_state.fan_duty, s_state.balance, s_state.tc_temp,
           s_state.junction_temp, s_state.tc_status, s_state.tc_error_count);
  ESP_LOGI(TAG, "Memory heap: %lu, min: %lu\n.\n", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
  return ret;

  heat_off:
  ESP_LOGE(TAG, "Shutting off heaters due to safety");
  s_state.input_duty = 0;
  s_state.output_duty = 0;
  ssr_ctrl_set_duty(s_ssr1, s_state.input_duty);
  ssr_ctrl_set_duty(s_ssr2, s_state.output_duty);
  return ret;
}

control_state_t controller_get_state() {
  return s_state;
}

control_cfg_t controller_get_cfg() {
  return s_cfg;
}

esp_err_t controller_set_cfg(control_cfg_t cfg) {
  if (cfg.max_board_temp < 50 or cfg.max_board_temp > 85) {
    ESP_LOGE(TAG, "Max board temperature invalid: %d : [%d, %d]", cfg.max_board_temp, 50, 85);
    goto error;
  }

  if (cfg.max_tc_temp > 300 or cfg.max_tc_temp < 200) {
    ESP_LOGE(TAG, "Invalid max TC temperature: %d, expected [200, 300]", cfg.max_tc_temp);
    goto error;
  }

  if (cfg.max_heat_ratio > 1.0 or cfg.max_heat_ratio < 0.0) {
    ESP_LOGE(TAG, "Invalid heat ratio too high: %f, expected [0, 1]", cfg.max_heat_ratio);
    goto error;
  }

  if (cfg.mains_hz != MAINS_50_HZ and cfg.mains_hz != MAINS_60_HZ) {
    ESP_LOGW(TAG, "Invalid mains frequency: %dHz, expected 50Hz or 60Hz]", cfg.mains_hz);
    goto error;
  }

  s_cfg = cfg;
  utils_save_to_nvs("controller", "cfg", &s_cfg, sizeof(control_cfg_t));
  ESP_LOGI(TAG, "New configuration set max_board_temp=%d, max_tc_temp=%d, max_heat_ratio=%f, mains_hz=%d",
           s_cfg.max_board_temp, s_cfg.max_tc_temp, s_cfg.max_heat_ratio, s_cfg.mains_hz);
  return ESP_OK;

  error:
  return ESP_FAIL;
}


void control_loop_run() {
  semaphoreHandle = xSemaphoreCreateBinary();
  TaskHandle_t xTaskToNotify = xTaskGetCurrentTaskHandle();
  // As this is a control loop, we want it to be very high priority
  vTaskPrioritySet(xTaskToNotify, 7);

  ESP_ERROR_CHECK(gptimer_start(gptimer));
  _go = true;

  // Add this task to the watch dog timer
  ESP_ERROR_CHECK(esp_task_wdt_add(xTaskToNotify));

  // We are good to go
  level_shifter_enable(true);

  ssr_ctrl_power_on(s_ssr1);
  ssr_ctrl_power_on(s_ssr2);

  // Go to go
  esp_event_post(MAIN_APP_EVENT, APP_READY, NULL, 0, portMAX_DELAY);

  do {
    if (xSemaphoreTake(semaphoreHandle, pdMS_TO_TICKS(1000)) == pdPASS) {
      // Restart if we are asked to
      if (reset_button_is_triggered()) {
        esp_restart();
      }

      ESP_ERROR_CHECK(esp_task_wdt_reset());
      _control();
    }
  } while (_go);

  ssr_ctrl_power_off(s_ssr1);
  ssr_ctrl_power_off(s_ssr2);
  level_shifter_enable(false);
  ssr_ctrl_del(s_ssr1);
  ssr_ctrl_del(s_ssr2);
  input_pwm_del(s_heat_pwm_in);
  input_pwm_del(s_fan_pwm_in);
}

void control_loop_stop() {
  ESP_ERROR_CHECK(gptimer_stop(gptimer));
  _go = false;
}

void control_loop_init(EventGroupHandle_t net_group) {
  utils_load_from_nvs("controller", "cfg", &s_cfg, sizeof(control_cfg_t));
  pm_control_init();
  reset_button_init(GPIO_NUM_4);
  level_shifter_init();
  panel_inputs_init();
  balancer_init();
  digital_input_init(DRUM_MOTOR_SIGNAL_PIN);
  telemetry_init(net_group);
  app_config_init();

  // Thermocouple amplifier
  max3185_devices_t found_devices = max31850_list(ONEWIRE_PIN);
  ESP_ERROR_CHECK(found_devices.devices_address_length == 1 ? ESP_OK : ESP_FAIL);
  s_max31850_addr = found_devices.devices_address[0];

  // Init reading inbound PWMs, noting heat is 2s period on later hottop models
  input_pwm_new({.gpio=HEAT_SIGNAL_PIN, .edge_type=PWM_INPUT_DOWN_EDGE_ON, .period_us=2100000}, &s_heat_pwm_in);
  input_pwm_new({.gpio=FAN_SIGNAL_PIN, .edge_type=PWM_INPUT_DOWN_EDGE_ON, .period_us=100000}, &s_fan_pwm_in);

  // Init SSRs
  ssr_ctrl_new({.gpio = SSR1_PIN, .mains_hz = (main_hertz_t) s_cfg.mains_hz}, &s_ssr1);
  ssr_ctrl_new({.gpio = SSR2_PIN, .mains_hz = (main_hertz_t) s_cfg.mains_hz}, &s_ssr2);

  // Turn off heat
  ssr_ctrl_set_duty(s_ssr1, 0);
  ssr_ctrl_set_duty(s_ssr2, 0);

  // Set up timer now
  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1 * 1000 * 1000, // 1MHz, 1 tick = 1us
      .flags = {.intr_shared = 1}
  };
  ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

  gptimer_alarm_config_t alarm_config = {
      .alarm_count = INTERVAL,
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = true},
  };
  ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

  gptimer_event_callbacks_t cbs = {
      .on_alarm = _on_alarm_cb,
  };
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(gptimer));
}