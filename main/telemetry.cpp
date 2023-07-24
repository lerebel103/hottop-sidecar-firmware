#include "telemetry.h"
#include "core_mqtt_serializer.h"
#include "mqtt/mqtt_client.h"
#include "common/identity.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "defender.h"
#include "core_mqtt.h"
#include "mqtt/mqtt_subscription_manager.h"
#include "wifi/wifi_connect.h"
#include "sntp/sntp_sync.h"
#include "app_metrics.h"
#include "device_info.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <cstring>
#include <ctime>
#include "control_loop.h"

#define TAG "telemetry"
#define TELEMETRY_INTERVAL_USEC (5000000)
#define TOPIC_MAX_SIZE (128)
#define PAYLOAD_MAX_SIZE (1024)
#define METRICS_INTERVAL_US (60*1e6)

static EventGroupHandle_t xNetworkEventGroup;
static bool _go = false;
static uint64_t _last_metrics_time = 0;

static char info_topic[TOPIC_MAX_SIZE];
static char payload[PAYLOAD_MAX_SIZE];


static void _send_status() {
  static const char* format = R"({
    "timestamp": %)" PRIu32 R"(,
    "loop_count": %)" PRIu32 R"(,
    "tc_temp": %f,
    "junction_temp": %f,
    "tc_status": %)" PRIu8 R"(,
    "tc_error_count": %)" PRIu32 R"(,
    "motor_on": %)" PRIu8 R"(,
    "fan_duty": %)" PRIu8 R"(,
    "balance": %f,
    "input_duty": %)" PRIu8 R"(,
    "output_duty": %)" PRIu8 R"(
  })";
  auto control_state = controller_get_state();
  sprintf(payload, format, (uint32_t)time(nullptr),
          control_state.loop_count,
          control_state.tc_temp,
          control_state.junction_temp,
          control_state.tc_status,
          control_state.tc_error_count,
          control_state.motor_on,
          control_state.fan_duty,
          control_state.balance,
          control_state.input_duty,
          control_state.output_duty);

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = info_topic,
      .topicNameLength = (uint16_t) strlen(info_topic),
      .pPayload = payload,
      .payloadLength = strlen(payload),
  };

  printf("%s\n", payload);

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

      ESP_LOGD(TAG, "Sending payloads %" PRIu64 " %" PRIu64, (now - _last_metrics_time), (uint64_t)METRICS_INTERVAL_US);

      if (device_info_update_required()) {
        device_info_send(payload, PAYLOAD_MAX_SIZE);
      }
      if (app_metrics_update_required()) {
        app_metrics_send(payload, PAYLOAD_MAX_SIZE);
      }

      _send_status();
    }

    uint64_t delta = esp_timer_get_time() - now;
    if (delta < TELEMETRY_INTERVAL_USEC) {
      vTaskDelay(pdMS_TO_TICKS((TELEMETRY_INTERVAL_USEC - delta))/1000);
    }
  } while (_go);
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    // Refresh metrics on new connection
    app_metrics_send(payload, PAYLOAD_MAX_SIZE);
  }
}

void telemetry_init(EventGroupHandle_t net_group) {
  if (_go) {
    return;
  }

  xNetworkEventGroup = net_group;
  device_info_init();
  app_metrics_init();

  // Regular telemetry
  sprintf(info_topic, "%s/%s/telemetry/status", CMAKE_THING_TYPE, identity_thing_id());

  esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr);

  _go = true;
  xTaskCreate(_send_telemetry, "send_telemetry", 3072, nullptr, 4, nullptr);
}
