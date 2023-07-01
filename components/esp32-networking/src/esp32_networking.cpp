#include "esp32_networking.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_event.h>

#include "wifi/wifi_connect.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "common/identity.h"
#include "ota/mqtt_ota.h"
#include "mqtt/mqtt_client.h"
#include "common/nvs.h"

static EventGroupHandle_t xNetworkEventGroup;

void esp32_networking_init() {
  xNetworkEventGroup = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  nvs_init();
  identity_get();

  mqtt_ota_init(xNetworkEventGroup);
  mqtt_provision_init(xNetworkEventGroup);
  mqtt_client_init(xNetworkEventGroup);
  wifi_connect_init(xNetworkEventGroup);
}
