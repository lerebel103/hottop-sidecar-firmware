#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "square_wave_gen.h"
#include <esp_check.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_timer.h>

#define TAG "sync_signal"

// 1MHz
#define RESOLUTION 1000000

struct square_wave_t {
  square_wave_cfg_t cfg;
  gptimer_handle_t timer_handle;
  SemaphoreHandle_t semaphoreHandle;
  bool go;
  uint64_t down_edge_time_us;
};

static bool _on_alarm_cb(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *user_ctx) {
  auto handle = (square_wave_t *) user_ctx;
  handle->down_edge_time_us = esp_timer_get_time();
  gpio_set_level(handle->cfg.gpio, 0);

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xSemaphoreGiveFromISR(handle->semaphoreHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  return true;
}

static void _generate_edge(void *data) {
  auto handle = (square_wave_t *) data;
  do {
    auto ret = xSemaphoreTake(handle->semaphoreHandle, pdMS_TO_TICKS(1000));
    if (ret == pdPASS) {
      esp_rom_delay_us(handle->cfg.low_us - (esp_timer_get_time() - handle->down_edge_time_us));
      gpio_set_level(handle->cfg.gpio, 1);
    }
  } while (handle->go);

  vSemaphoreDelete(handle->semaphoreHandle);
  handle->semaphoreHandle = nullptr;

  // Clean up self.
  vTaskDelete(nullptr);
}


esp_err_t square_wave_gen_start(square_wave_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  return gptimer_start(handle->timer_handle);
}

esp_err_t square_wave_gen_stop(square_wave_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  return gptimer_stop(handle->timer_handle);
}

static esp_err_t square_wave_destroy(square_wave_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  if (handle->timer_handle) {
    gptimer_disable(handle->timer_handle);
    gptimer_del_timer(handle->timer_handle);
  }

  // Wait for loop to finish
  handle->go = false;
  do {} while (handle->semaphoreHandle != nullptr);

  free(handle);
  return ESP_OK;
}

esp_err_t square_wave_gen_del(square_wave_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  ESP_LOGI(TAG, "Deleting wave generator on gpio %d deleted", handle->cfg.gpio);
  // recycle memory resource
  ESP_RETURN_ON_ERROR(square_wave_destroy(handle), TAG, "destroy generator failed");
  return ESP_OK;
}

esp_err_t square_wave_gen_new(square_wave_cfg_t cfg, square_wave_handle_t *ret_handle) {
  esp_err_t ret = ESP_OK;
  gpio_config_t io_conf;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (
      (1ULL << cfg.gpio)
  );
  io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  // Set up timer now
  gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = RESOLUTION,
      .flags = {.intr_shared = 1}
  };
  gptimer_alarm_config_t alarm_config = {
      .alarm_count = cfg.period_us,
      .reload_count = 0,
      .flags = {.auto_reload_on_alarm = true},
  };
  gptimer_event_callbacks_t cbs = {
      .on_alarm = _on_alarm_cb,
  };

  // Do allocation for handle and go
  square_wave_t *handle;
  ESP_GOTO_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
  handle = (square_wave_t *) heap_caps_calloc(1, sizeof(square_wave_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG, "no mem for square wave generator");

  handle->cfg = cfg;

  ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "Failed to configure GPIO");
  ESP_GOTO_ON_ERROR(gptimer_new_timer(&timer_config, &handle->timer_handle), err, TAG, "Failed to create new timer");
  ESP_GOTO_ON_ERROR(gptimer_set_alarm_action(handle->timer_handle, &alarm_config), err, TAG, "Failed ot set alarm");
  ESP_GOTO_ON_ERROR(gptimer_register_event_callbacks(handle->timer_handle, &cbs, handle), err, TAG,
                    "Failed to register event");
  ESP_GOTO_ON_ERROR(gptimer_enable(handle->timer_handle), err, TAG, "Could not enable timer");

  // Set flag to run thread to create other edge
  handle->go = true;
  handle->semaphoreHandle = xSemaphoreCreateBinary();
  xTaskCreate(_generate_edge, "generateEdge", 1024, handle, 5, nullptr);

  // Good to go
  ESP_LOGI(TAG, "Wave generator created on gpio %d deleted", handle->cfg.gpio);
  *ret_handle = handle;
  return ret;

  err:
  if (ret_handle) {
    square_wave_destroy(handle);
  }
  return ret;
}