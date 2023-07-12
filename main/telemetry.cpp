#include "telemetry.h"
#include "core_mqtt_serializer.h"
#include "mqtt/mqtt_client.h"
#include "common/identity.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <cstring>

#define TAG "telemetry"
#define TELEMETRY_INTERVAL_USEC (5000000)
#define TOPIC_MAX_SIZE (128)
#define PAYLOAD_MAX_SIZE (512)

static EventGroupHandle_t xNetworkEventGroup;
static bool _go = false;
bool _device_info_requested = false;

static char status_topic[TOPIC_MAX_SIZE];
static char info_topic[TOPIC_MAX_SIZE];
static char payload[PAYLOAD_MAX_SIZE];

static void _send_device_info() {

}

static void _send_status() {

  strcpy(payload, "{\"test\": 1}");

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = status_topic,
      .topicNameLength = (uint16_t) strlen(status_topic),
      .pPayload = payload,
      .payloadLength = strlen(payload),
  };

  // Send as best effort, not fussed
  mqtt_client_publish(&publishInfo, 0);
}

static void _send_telemetry(void *) {
  do {
    uint64_t now = esp_timer_get_time();

    // Wait for MQTT and ensure we are not doing an OTA
    xEventGroupWaitBits(xNetworkEventGroup,
                        CORE_MQTT_CLIENT_CONNECTED_BIT,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY);

    bool ota_in_progress = xEventGroupGetBits(xNetworkEventGroup) & CORE_MQTT_OTA_IN_PROGRESS_BIT;
    if (!mqtt_provisioning_active() && !ota_in_progress) {
      ESP_LOGI(TAG, "Sending payload");

      if (_device_info_requested) {
        _send_device_info();
        _device_info_requested = false;
      }
      _send_status();
    }

    uint64_t delta = esp_timer_get_time() - now;
    if (delta < TELEMETRY_INTERVAL_USEC) {
      esp_rom_delay_us(TELEMETRY_INTERVAL_USEC - delta);
    }
  } while (_go);
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    _device_info_requested = true;
  }
}

void telemetry_init(EventGroupHandle_t net_group) {
  if (_go) {
    return;
  }

  xNetworkEventGroup = net_group;
  sprintf(status_topic, "%s/%s/telemetry/status", CMAKE_THING_TYPE, identity_thing_id());
  sprintf(info_topic, "%s/%s/telemetry/device_info", CMAKE_THING_TYPE, identity_thing_id());
  esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr);

  _go = true;
  xTaskCreate(_send_telemetry, "send_telemetry", 3072, nullptr, 5, nullptr);
}
