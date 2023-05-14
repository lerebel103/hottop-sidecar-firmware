#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gptimer.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <freertos/semphr.h>
#include "control_loop.h"
#include "ssr_ctrl.h"
#include "max31850.h"
#include "level_shifter.h"
#include "panel_inputs.h"
#include "balancer.h"
#include "input_pwm_duty.h"
#include "digital_input.h"

// Interval in MHz
#define INTERVAL 1000000
#define TAG "control"

#define ONEWIRE_PIN                 GPIO_NUM_2
#define HEAT_SIGNAL_PIN             GPIO_NUM_6
#define DRUM_MOTOR_SIGNAL_PIN       GPIO_NUM_7
#define FAN_SIGNAL_PIN              GPIO_NUM_9
#define SSR1_PIN                    GPIO_NUM_10
#define SSR2_PIN                    GPIO_NUM_11

#define MAX_SECONDARY_HEAT_RATIO    0.7f
#define TEMPERATURE_TC_MAX          270.0f
#define TEMPERATURE_BOARD_MAX       75.0f
#define MAINS_HZ                    MAINS_50_HZ

static SemaphoreHandle_t semaphoreHandle;
static gptimer_handle_t gptimer;
static bool _go = false;

static input_pwm_handle_t s_heat_pwm_in;
static input_pwm_handle_t s_fan_pwm_in;
static ssr_ctrl_handle_t s_ssr1 = nullptr;
static ssr_ctrl_handle_t s_ssr2 = nullptr;
static uint64_t s_max31850_addr = 0;


/*
 * Checks that the TC and environmental temperatures are within acceptable range
 */
static bool _temperature_ok() {
  bool is_ok = false;

  // Get TC value
  max31850_data_t elm_temp = max31850_read(ONEWIRE_PIN, s_max31850_addr);
  if (elm_temp.is_valid) {
    if (elm_temp.thermocouple_status == MAX31850_TC_STATUS_OK) {
      ESP_LOGI(TAG, "Thermocouple=%.2fC, Board=%.2fC", elm_temp.thermocouple_temp, elm_temp.junction_temp);

      // We want to make sure we are under max temperatures allowed
      if (elm_temp.thermocouple_temp <= TEMPERATURE_TC_MAX && elm_temp.junction_temp < TEMPERATURE_BOARD_MAX) {
        is_ok = true;
      } else {
        ESP_LOGW(TAG, "Max temperature exceed Max TC=%.2f, Max Board=%.2f", TEMPERATURE_TC_MAX,
                 TEMPERATURE_BOARD_MAX);
      }
    } else {
      if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_OPEN_CIRCUIT) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault OPEN CIRCUIT");
      } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_GND) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO GROUND");
      } else if (elm_temp.thermocouple_status & MAX31850_TC_STATUS_SHORT_VCC) {
        ESP_LOGE(TAG, "Unable to run, thermocouple fault SHORT TO VCC");
      }
    }
  } else {
    ESP_LOGE(TAG, "Unable to run, error reading thermocouple data.");
  }

  return is_ok;
}

/*
 * Ensures conditions are met so we can start controlling
 *
 * The temperatures read must be within allowed ranges.
 * The main drum must be spinning.
 */
static bool _can_control() {
  bool is_ok = _temperature_ok();

  return is_ok;
}

static bool _on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(semaphoreHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  return true;
}

/* Do it, one loop iteration */
static void _control() {
  uint8_t input_duty;
  ESP_ERROR_CHECK(input_pwm_get_duty(s_heat_pwm_in, input_duty));
  uint8_t fan_duty;
  ESP_ERROR_CHECK(input_pwm_get_duty(s_fan_pwm_in, fan_duty));

  uint8_t output_duty = 0;
  double balance = balance_read_percent();

  bool motor_on = digital_input_is_on(DRUM_MOTOR_SIGNAL_PIN);
  if (!motor_on) {
    // Can't apply heat to stationary drum, bad news
    input_duty = 0;
  }

  if (_can_control()) {
    output_duty = (uint8_t)(input_duty * balance / 100.0 * MAX_SECONDARY_HEAT_RATIO);
  }

  ESP_LOGI(TAG, "Motor=%d, Balance: %f, Input=%d, Output=%d, Fan=%d",
           motor_on, balance, input_duty, output_duty, fan_duty);

  ssr_ctrl_set_duty(s_ssr1, input_duty);
  ssr_ctrl_set_duty(s_ssr2, output_duty);

  ESP_LOGI(TAG, "Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
  ESP_LOGI(TAG, ".");
}

void control_loop_run() {
  semaphoreHandle = xSemaphoreCreateBinary();

  TaskHandle_t xTaskToNotify = xTaskGetCurrentTaskHandle();
  ESP_ERROR_CHECK(gptimer_start(gptimer));
  _go = true;

  esp_task_wdt_config_t wdt_cfg = {
      .timeout_ms= (int) (INTERVAL * 1.5),
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
      .trigger_panic=true
  };

#if !CONFIG_ESP_TASK_WDT_INIT
  ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_cfg));
#endif
  ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&wdt_cfg));
  ESP_ERROR_CHECK(esp_task_wdt_add(xTaskToNotify));

  // We are good to go
  level_shifter_enable(true);

  ssr_ctrl_power_on(s_ssr1);
  ssr_ctrl_power_on(s_ssr2);

  do {
    if (xSemaphoreTake(semaphoreHandle, pdMS_TO_TICKS(1000)) == pdPASS) {
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

void control_loop_init() {
  level_shifter_init();
  panel_inputs_init();
  balancer_init();
  digital_input_init(DRUM_MOTOR_SIGNAL_PIN);

  // Thermocouple amplifier
  max3185_devices_t found_devices = max31850_list(ONEWIRE_PIN);
  ESP_ERROR_CHECK(found_devices.devices_address_length == 1 ? ESP_OK : ESP_FAIL);
  s_max31850_addr = found_devices.devices_address[0];

  // Init reading inbound PWMs, noting heat is 2s period on later hottop models
  input_pwm_new({.gpio=HEAT_SIGNAL_PIN, .edge_type=PWM_INPUT_DOWN_EDGE_ON, .period_us=2100000}, &s_heat_pwm_in);
  input_pwm_new({.gpio=FAN_SIGNAL_PIN, .edge_type=PWM_INPUT_DOWN_EDGE_ON, .period_us=100000}, &s_fan_pwm_in);

  // Init SSRs
  ssr_ctrl_new({.gpio = SSR1_PIN, .mains_hz = MAINS_HZ}, &s_ssr1);
  ssr_ctrl_new({.gpio = SSR2_PIN, .mains_hz = MAINS_HZ}, &s_ssr2);

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