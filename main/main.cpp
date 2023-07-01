#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/event_groups.h>
#include <cstring>
#include "control_loop.h"
#include "square_wave_gen.h"
#include "wifi/wifi_connect.h"
#include "nvs.h"
#include "mqtt/mqtt_client.h"
#include "events_common.h"
#include "esp_pm.h"
#include "core_mqtt.h"
#include "shadow/shadow_handler.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "mqtt/identity.h"
#include "ota/mqtt_ota.h"
#include "ota.h"

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


void get_shadow_handler(MQTTContext_t * ctx, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Receive shadow topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGI(TAG, "Shadow ps %.*s",
           pxPublishInfo->payloadLength, (const char*)pxPublishInfo->pPayload);
}



extern "C" void app_main() {
//  esp_deep_sleep(100000000);

  /*const esp_partition_t * update_partition = esp_ota_get_next_update_partition( NULL );
  esp_ota_handle_t update_handle;
  esp_err_t err = esp_ota_begin( update_partition, OTA_SIZE_UNKNOWN, &update_handle );
  if( err != ESP_OK ) {
    ESP_LOGE(TAG, "Crap %d", err);
  }
  ESP_LOGI(TAG, "this works");
  return;*/

  //_generate_zero_signal();

  xNetworkEventGroup = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  gpio_install_isr_service(0);

  nvs_init();

  // Enable power management and light sleep
  /*esp_pm_config_esp32s3_t pm_config = {
      .max_freq_mhz = 160,
      .min_freq_mhz = 80,
      .light_sleep_enable = true
  };
  ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );*/


  esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
  };
  ESP_ERROR_CHECK( esp_pm_configure(&pm_config) );

  device_shadow_cfg_t shadow_cfg = {.name = "config", .get = get_shadow_handler};
  device_shadow_handle_t shadow_handle;
  shadow_handler_init(shadow_cfg, &shadow_handle);

  mqtt_ota_init(xNetworkEventGroup);
  mqtt_provision_init(xNetworkEventGroup);
  mqtt_client_init(xNetworkEventGroup);
  wifi_connect_init(xNetworkEventGroup);
  //esp_wifi_set_ps(WIFI_PS_MAX_MODEM);


  ESP_LOGI(TAG, "Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
/*  do {
    xEventGroupWaitBits( xNetworkEventGroup,
                         CORE_MQTT_CLIENT_CONNECTED_BIT, pdFALSE, pdTRUE,
                         portMAX_DELAY );

    ESP_LOGW(TAG, "----- Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    vTaskDelay(pdMS_TO_TICKS(30000));

    mqtt_client_disconnect();
    identity_reload();
    mqtt_client_init(xNetworkEventGroup);

    //mqtt_client_subscribe(topics, 2, timeout);
    //vTaskDelay(pdMS_TO_TICKS(5000));
    //mqtt_client_unsubscribe(topics2, 1, timeout);

    //vTaskDelay(pdMS_TO_TICKS(5000));

  }while(1);

  //esp_rom_delay_us(10000000);
*/


  do {
    ESP_LOGI(TAG, "Memory heap: %lu, min: %lu", esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    printf("Ota state=%d, image_state=%d, in OTA: %lu\n", OTA_GetState(), OTA_GetImageState(),  (xEventGroupGetBits(xNetworkEventGroup) & CORE_MQTT_OTA_IN_PROGRESS_BIT));

    vTaskDelay(pdMS_TO_TICKS(1000));
  } while(1);


  //control_loop_init();
  //control_loop_run();
}

