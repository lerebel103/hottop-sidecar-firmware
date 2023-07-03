#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <cstring>
#include <esp_wifi_types.h>
#include <esp_wifi.h>
#include "control_loop.h"
#include "square_wave_gen.h"
#include "esp_pm.h"
#include "shadow/shadow_handler.h"
#include "ota.h"
#include "esp32_networking.h"
#include "common/events_common.h"

#define TAG  "main"

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


void get_shadow_handler(MQTTContext_t * ctx, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Receive shadow topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGI(TAG, "Shadow ps %.*s",
           pxPublishInfo->payloadLength, (const char*)pxPublishInfo->pPayload);
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT || event_id == CORE_MQTT_OTA_STOPPED_EVENT) {

    // Enable power management and light sleep
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );

    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  } else if(event_id == CORE_MQTT_DISCONNECTED_EVENT || event_id == CORE_MQTT_OTA_STARTED_EVENT) {

    // Enable power management and light sleep
    esp_pm_config_esp32s3_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false
    };
    ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );

    esp_wifi_set_ps(WIFI_PS_NONE);
  }
}

extern "C" void app_main() {
  // esp_deep_sleep(100000000);
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  gpio_install_isr_service(0);

  device_shadow_cfg_t shadow_cfg = {.name = "config", .get = get_shadow_handler};
  device_shadow_handle_t shadow_handle;
  shadow_handler_init(shadow_cfg, &shadow_handle);

  esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr);
  esp32_networking_init();

  // Control loop
  control_loop_init();
  control_loop_run();

  do {
    ESP_LOGI(TAG, "Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    vTaskDelay(pdMS_TO_TICKS(1000));
  } while(1);

}

