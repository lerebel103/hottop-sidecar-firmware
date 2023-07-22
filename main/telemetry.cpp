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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_event.h>
#include <cstring>
#include <ctime>

#define TAG "telemetry"
#define TELEMETRY_INTERVAL_USEC (5000000)
#define TOPIC_MAX_SIZE (128)
#define PAYLOAD_MAX_SIZE (1024)
#define METRICS_INTERVAL_US (60*1e6)

static EventGroupHandle_t xNetworkEventGroup;
static bool _go = false;
static bool _defender_metrics_requested = false;
static uint64_t _last_metrics_time = 0;

static char defender_topic_pub[TOPIC_MAX_SIZE];
static char defender_topic_acc[TOPIC_MAX_SIZE];
static char defender_topic_rej[TOPIC_MAX_SIZE];

static char info_topic[TOPIC_MAX_SIZE];
static char payload[PAYLOAD_MAX_SIZE];

//"fw_version": [{"value": "%d.%d.%d"}],


static void _send_network_metrics() {
  static const char* metrics_format =
      R"(
    {
      "header": {
        "report_id": %)" PRIu32 R"(,
        "version": "1.0"
      },
      "metrics": {},
      "custom_metrics": {
        "wifi.connect_attempt_count": %)" PRIu32 R"(,
        "wifi.disconnected_count": %)" PRIu32 R"(,
        "wifi.connected_count": %)" PRIu32 R"(,
        "wifi.connect_duration_ms": %)" PRIu32 R"(,
        "wifi.rssi": %)" PRId8 R"(,
        "wifi.channel": %)" PRIu8 R"(,
        "wifi.ssid": %s,
        "wifi.ap_bssid": %s,
        "wifi.ip_addr": %s,
        "wifi.gw_addr": %s,
        "wifi.nm_addr": %s

        "sntp.last_sync_time": %)" PRIu64 R"(,
        "sntp.sync_duration_ms": %)" PRIu32 R"(,

        "mqtt.connect_attempt_count": %)" PRIu32 R"(,
        "mqtt.disconnected_count": %)" PRIu32 R"(,
        "mqtt.connected_count": %)" PRIu32 R"(,
        "mqtt.connect_duration_ms": %)" PRIu32 R"(,
        "mqtt.tx_pkt_count": %)" PRIu32 R"(,
        "mqtt.tx_bytes_count": %)" PRIu64 R"(,
        "mqtt.rx_pkt_count": %)" PRIu32 R"(,
        "mqtt.rx_bytes_count": %)" PRIu64 R"(,
    }
    )";
  time_t report_id = time(nullptr);

  auto wifi_metrics = wifi_connect_get_metrics();
  auto mqtt_metrics = mqtt_client_get_metrics();
  auto sntp_metrics = sntp_sync_get_metrics();

  sprintf(payload, metrics_format,
          (uint32_t)report_id, wifi_metrics.connect_attempt_count, wifi_metrics.disconnected_count,
          wifi_metrics.connected_count, wifi_metrics.connect_duration_ms, wifi_metrics.rssi, wifi_metrics.channel,
          wifi_metrics.ssid, wifi_metrics.ap_bssid, wifi_metrics.ip_addr,  wifi_metrics.gw_addr, wifi_metrics.nm_addr,
          (uint64_t)sntp_metrics.last_sync_time, sntp_metrics.sync_duration_ms,
          mqtt_metrics.connect_attempt_count, mqtt_metrics.disconnected_count, mqtt_metrics.connected_count,
          mqtt_metrics.connect_duration_ms,
          mqtt_metrics.tx_pkt_count, mqtt_metrics.tx_bytes_count,
          mqtt_metrics.rx_pkt_count, mqtt_metrics.rx_bytes_count);

  printf("%s\n", payload);
}

static void _send_defender_metrics() {
  static const char* metrics_format =
      R"(
    {
      "header": {
        "report_id": %)" PRIu32 R"(,
        "version": "1.0"
      },
      "metrics": {},
      "custom_metrics": {
        "uptime": [{"number": %)" PRIu32 R"(}],
        "heap_free": [{"number": %)" PRIu32 R"(}],
        "heap_min": [{"number": %)" PRIu32 R"(}]
      }
    }
    )";

  time_t report_id = time(nullptr);

  sprintf(payload, metrics_format,
          (uint32_t)report_id,
          //CMAKE_FIRMWARE_VERSION_MAJOR, CMAKE_FIRMWARE_VERSION_MINOR, CMAKE_FIRMWARE_VERSION_BUILD,
          (uint32_t)(esp_timer_get_time() / 1e6),
          (uint32_t)esp_get_free_heap_size(),
          (uint32_t)esp_get_minimum_free_heap_size());

  printf("%s\n", payload);

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = defender_topic_pub,
      .topicNameLength = (uint16_t) strlen(defender_topic_pub),
      .pPayload = payload,
      .payloadLength = strlen(payload),
  };

  // Send as best effort, not fussed
  mqtt_client_publish(&publishInfo, 0);

  _send_network_metrics();
}

static void _send_status() {

  strcpy(payload, "{\"test\": 1}");

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = info_topic,
      .topicNameLength = (uint16_t) strlen(info_topic),
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

      if ((now - _last_metrics_time) > METRICS_INTERVAL_US ) {
        _defender_metrics_requested = true;
      }
      ESP_LOGD(TAG, "Sending payloads %" PRIu64 " %" PRIu64, (now - _last_metrics_time), (uint64_t)METRICS_INTERVAL_US);

      if (_defender_metrics_requested) {
        _send_defender_metrics();
        _last_metrics_time = now;
        _defender_metrics_requested = false;
      }
      _send_status();
    }

    uint64_t delta = esp_timer_get_time() - now;
    if (delta < TELEMETRY_INTERVAL_USEC) {
      vTaskDelay(pdMS_TO_TICKS((TELEMETRY_INTERVAL_USEC - delta))/1000);
    }
  } while (_go);
}


static void _defender_metrics_acc(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Defender metrics accepted");
}

static void _defender_metrics_rej(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGE(TAG, "Defender metrics rejected: %.*s", pxPublishInfo->payloadLength, (char*)pxPublishInfo->pPayload);
}


static void _subscribe() {

  ESP_LOGI(TAG, "Subscribing to shadow topics");
  static const int NUM_SUBSCRIPTIONS = 2;
  MQTTSubscribeInfo_t subscribeInfo[NUM_SUBSCRIPTIONS] = {
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = defender_topic_acc,
          .topicFilterLength = (uint16_t) strlen(defender_topic_acc)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = defender_topic_rej,
          .topicFilterLength = (uint16_t) strlen(defender_topic_rej)
      },
  };

  // Nothing will work if we don't have subscriptions,
  // this will block the event queue but for good reasons
  mqtt_client_subscribe(subscribeInfo, NUM_SUBSCRIPTIONS, UINT16_MAX);

  // Register callbacks
  SubscriptionManager_RegisterCallback(defender_topic_acc, strlen(defender_topic_acc), _defender_metrics_acc);
  SubscriptionManager_RegisterCallback(defender_topic_rej, strlen(defender_topic_rej), _defender_metrics_rej);
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    _subscribe();
    _defender_metrics_requested = true;

    _send_network_metrics();
  }
}

void telemetry_init(EventGroupHandle_t net_group) {
  if (_go) {
    return;
  }

  xNetworkEventGroup = net_group;

  // Setup defender topic
  const char* thing_name = identity_thing_id();
  uint16_t topic_len;
  Defender_GetTopic(defender_topic_pub, TOPIC_MAX_SIZE, thing_name, strlen(thing_name), DefenderJsonReportPublish, &topic_len);
  defender_topic_pub[topic_len] = '\0';

  Defender_GetTopic(defender_topic_acc, TOPIC_MAX_SIZE, thing_name, strlen(thing_name), DefenderJsonReportAccepted, &topic_len);
  defender_topic_acc[topic_len] = '\0';

  Defender_GetTopic(defender_topic_rej, TOPIC_MAX_SIZE, thing_name, strlen(thing_name), DefenderJsonReportRejected, &topic_len);
  defender_topic_rej[topic_len] = '\0';

  // Regular telemetry
  sprintf(info_topic, "%s/%s/telemetry/device_info", CMAKE_THING_TYPE, identity_thing_id());

  esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr);

  _go = true;
  xTaskCreate(_send_telemetry, "send_telemetry", 3072, nullptr, 4, nullptr);


}
