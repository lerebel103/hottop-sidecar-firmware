#include "esp32_networking.h"

#include <esp_app_desc.h>

#include "wifi/wifi_connect.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "common/identity.h"
#include "ota/mqtt_ota.h"
#include "mqtt/mqtt_client.h"
#include "common/nvs.h"

#define TAG "networking"

static EventGroupHandle_t xNetworkEventGroup;

void esp32_networking_init(EventGroupHandle_t net_group) {
  xNetworkEventGroup = net_group;

  ESP_ERROR_CHECK( nvs_init() );
  auto identity = identity_get();
  ESP_LOGI(TAG, "--------------------------------------------------------------------------------");
  ESP_LOGI(TAG, "    Thing ID:      %s", identity_thing_id());
  ESP_LOGI(TAG, "    Thing Type:    %s", identity->thing_type);
  ESP_LOGI(TAG, "    Hardware rev:  %d.%d", identity->hardware_major, identity->hardware_minor);
  ESP_LOGI(TAG, "    Firmware ver:  %s", esp_app_get_description()->version);
  ESP_LOGI(TAG, "--------------------------------------------------------------------------------");

  ESP_ERROR_CHECK( mqtt_ota_init(xNetworkEventGroup) );
  ESP_ERROR_CHECK( mqtt_provision_init(xNetworkEventGroup) );
  ESP_ERROR_CHECK( mqtt_client_init(xNetworkEventGroup) );
  wifi_connect_init(xNetworkEventGroup);
}
