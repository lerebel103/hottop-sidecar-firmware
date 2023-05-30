#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/event_groups.h>
#include "control_loop.h"
#include "square_wave_gen.h"
#include "wifi/wifi_connect.h"
#include "nvs.h"
#include "mqtt/mqtt.h"
#include "mqtt/shadow_handler.h"

#define TAG  "main"

static square_wave_handle_t wave_handle;
static EventGroupHandle_t xNetworkEventGroup;
/**
 * Hototop needs a 100Hz square signal, with 480us low side.
 * We have the option of generating this signal here for testing purposes with a real panel
 */
void _generate_zero_signal() {
  // Here for debugging other boards or self, generates a reference 100Hz signal
  square_wave_cfg_t cfg = {
      .gpio = GPIO_NUM_34,
      .period_us = 10000,
      .low_us = 480
  };
  square_wave_gen_new(cfg, &wave_handle);
  square_wave_gen_start(wave_handle);
}

void get_shadow_handler(void *pvIncomingPublishCallbackContext, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Receive shadow topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGI(TAG, "Shadow ps %.*s",
           pxPublishInfo->payloadLength, (const char*)pxPublishInfo->pPayload);
}

extern "C" void app_main() {
  _generate_zero_signal();

  xNetworkEventGroup = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  gpio_install_isr_service(0);

  nvs_init();

  device_shadow_cfg_t shadow_cfg = {.name = "config", .get_accepted = get_shadow_handler};
  device_shadow_handle_t shadow_handle;
  shadow_handler_init(shadow_cfg, &shadow_handle);

  mqtt_init(xNetworkEventGroup);
  wifi_connect_init(xNetworkEventGroup);


  control_loop_init();
  control_loop_run();
}

