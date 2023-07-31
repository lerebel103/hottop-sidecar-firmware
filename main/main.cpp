#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <cstring>
#include <freertos/event_groups.h>
#include "control_loop.h"
#include "square_wave_gen.h"
#include "esp_pm.h"
#include "shadow/shadow_handler.h"
#include "ota.h"
#include "esp32_networking.h"
#include "events.h"

#define TAG  "main"

ESP_EVENT_DEFINE_BASE(MAIN_APP_EVENT);
static EventGroupHandle_t xNetworkEventGroup;

static square_wave_handle_t wave_handle;
/**
 * Hototop needs a 100Hz square signal, with 480us low side.
 * We have the option of generating this signal here for testing purposes with a real panel
 */
static void _generate_zero_signal() {
  // Here for debugging other boards or self, generates a reference 100Hz signal
  square_wave_cfg_t cfg = {
      .gpio = GPIO_NUM_34,
      .period_us = 10000,
      .low_us = 480
  };
  square_wave_gen_new(cfg, &wave_handle);
  square_wave_gen_start(wave_handle);
}


extern "C" void app_main() {
  //_generate_zero_signal();
  esp_log_level_set("coreMQTT", ESP_LOG_ERROR);

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  gpio_install_isr_service(0);
  xNetworkEventGroup = xEventGroupCreate();

  esp32_networking_init(xNetworkEventGroup);
  control_loop_init(xNetworkEventGroup);
  control_loop_run();

  do {
    ESP_LOGI(TAG, "Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    vTaskDelay(pdMS_TO_TICKS(1000));
  } while(1);

}

