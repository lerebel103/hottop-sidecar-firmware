#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "input_pwm_duty.h"
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_check.h>
#include <cmath>

#define TAG "input_pwm"

struct input_pwm_t {
  input_pwm_cfg_t cfg;

  uint64_t on_start;
  uint64_t period;
  uint64_t period_on;
  uint8_t duty;

  bool go;
  SemaphoreHandle_t semaphoreHandle;
};

static void IRAM_ATTR _handle_isr(void *ctx) {
  auto handle = (input_pwm_t *) ctx;

  // Here we track the first edge timing and time this state.
  // Then we look for the following opposite edge which gives us a period. The ratio of the two will later
  // give us a duty. This can't be calculated here in ISR (float operation)
  if (gpio_get_level(handle->cfg.gpio) == (handle->cfg.edge_type == PWM_INPUT_UP_EDGE_ON ? 1 : 0)) {
    uint64_t on_start = esp_timer_get_time();
    if (handle->on_start > 0) {
      handle->period = on_start - handle->on_start;
    }
    handle->on_start = esp_timer_get_time();

    // Notify task that calculates the duty
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(handle->semaphoreHandle, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  } else {
    handle->period_on = esp_timer_get_time() - handle->on_start;
  }
}

static void _calculate_duty(void *data) {
  auto handle = (input_pwm_t *) data;
  do {
    auto ret = xSemaphoreTake(handle->semaphoreHandle, pdMS_TO_TICKS(handle->cfg.period_us / 1000));
    if (ret == pdPASS && handle->period > handle->period_on) {
      handle->duty = (uint8_t) round(((double) handle->period_on / (double) handle->period) * 100);
    } else {
      // We've timed out and received no edges at all, so pick up the current GPIO state
      auto level = gpio_get_level(handle->cfg.gpio);
      if (handle->cfg.edge_type == PWM_INPUT_UP_EDGE_ON) {
        handle->duty = (level == 1 ? 100 : 0);
      } else {
        handle->duty = (level == 0 ? 100 : 0);
      }
    }
  } while (handle->go);

  vSemaphoreDelete(handle->semaphoreHandle);
  handle->semaphoreHandle = nullptr;

  // Clean up self.
  vTaskDelete(nullptr);
}

esp_err_t input_pwm_get_duty(input_pwm_handle_t handle, uint8_t &duty) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  duty = handle->duty;
  return ESP_OK;
}

static esp_err_t input_pwm_destroy(input_pwm_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

  gpio_isr_handler_remove(handle->cfg.gpio);
  // Wait for loop to finish
  handle->go = false;
  do {} while (handle->semaphoreHandle != nullptr);

  free(handle);
  return ESP_OK;
}


esp_err_t input_pwm_del(input_pwm_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  ESP_LOGI(TAG, "Deleting pwm input on gpio %d deleted", handle->cfg.gpio);
  // recycle memory resource
  ESP_RETURN_ON_ERROR(input_pwm_destroy(handle), TAG, "destroy failed");
  return ESP_OK;
}

esp_err_t input_pwm_new(input_pwm_cfg_t cfg, input_pwm_handle_t *ret_handle) {
  esp_err_t ret = ESP_OK;

  // --- Configure input switch that drives the pump
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_ANYEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (
      (1ULL << cfg.gpio)
  );

  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;

  input_pwm_t *handle;
  ESP_GOTO_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
  handle = (input_pwm_t *) heap_caps_calloc(1, sizeof(input_pwm_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG, "no mem for square wave generator");

  // Assign config
  handle->cfg = cfg;

  ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "io config error");
  ESP_GOTO_ON_ERROR(gpio_isr_handler_add(handle->cfg.gpio, _handle_isr, handle), err, TAG, "isr handler fail");

  // Set flag to run thread to create other edge
  handle->go = true;
  handle->semaphoreHandle = xSemaphoreCreateBinary();
  xTaskCreate(_calculate_duty, "PWM input", 1024, handle, 5, nullptr);

  // Good to go
  ESP_LOGI(TAG, "PWM input created for gpio %d", handle->cfg.gpio);
  *ret_handle = handle;
  return ret;

  err:
  if (ret_handle) {
    input_pwm_destroy(handle);
  }
  return ret;
}
