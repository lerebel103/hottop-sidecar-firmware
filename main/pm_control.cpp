#include <esp32s3/pm.h>
#include <esp_pm.h>
#include <esp_wifi_types.h>
#include <esp_wifi.h>
#include "pm_control.h"
#include "common/events_common.h"

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


void pm_control_init() {
  esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr);

}