#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <ctime>
#include <cstring>
#include <esp_event.h>
#include "app_metrics.h"
#include "shadow/shadow_handler.h"
#include "common/identity.h"
#include "wifi/wifi_connect.h"
#include "mqtt/mqtt_client.h"
#include "sntp/sntp_sync.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"

#define TAG "app_metrics"
#define NVS_STATS_NAMESPACE "stats"
#define TOPIC_MAX_SIZE 128

struct device_metrics_t {
  uint32_t boot_count;
  uint32_t crash_count;
  uint32_t last_crash_reason;
};

static device_metrics_t s_device_metrics = {};
static time_t _last_report_time = 0;
static char metrics_topic[TOPIC_MAX_SIZE];

static void _record_metrics() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_STATS_NAMESPACE, NVS_READWRITE, &nvs_handle));
  nvs_get_u32(nvs_handle, "boot_count", &s_device_metrics.boot_count);
  nvs_get_u32(nvs_handle, "crash_count", &s_device_metrics.crash_count);
  nvs_get_u32(nvs_handle, "last_crash_reason", &s_device_metrics.last_crash_reason);

  auto reason = esp_reset_reason();
  if (reason != ESP_RST_DEEPSLEEP && reason != ESP_RST_POWERON && reason != ESP_RST_SW) {
    ESP_LOGE(TAG, "Detected crash with reset reason: %d", reason);
    // Then we have a crash
    s_device_metrics.crash_count++;
    s_device_metrics.last_crash_reason = reason;
  }

  s_device_metrics.boot_count++;

  nvs_set_u32(nvs_handle, "boot_count", s_device_metrics.boot_count);
  nvs_set_u32(nvs_handle, "crash_count", s_device_metrics.crash_count);
  nvs_set_u32(nvs_handle, "last_crash_reason", s_device_metrics.last_crash_reason);

  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  nvs_close(nvs_handle);
}

void app_metrics_send(char *buffer, size_t max_len) {
  static const char *metrics_format =
      R"({
      "metrics": {
        "timestamp": %)" PRIu32 R"(,
        "boot_count": %)" PRIu32 R"(,
        "crash_count": %)" PRIu32 R"(,
        "last_crash_reason": %)" PRIu32 R"(,
        "heap_free": %)" PRIu32 R"(,
        "heap_min": %)" PRIu32 R"(,
        "wifi.connect_attempt_count": %)" PRIu32 R"(,
        "wifi.disconnected_count": %)" PRIu32 R"(,
        "wifi.connected_count": %)" PRIu32 R"(,
        "wifi.connect_duration_ms": %)" PRIu32 R"(,
        "wifi.rssi": %)" PRId8 R"(,
        "wifi.channel": %)" PRIu8 R"(,
        "wifi.ssid": "%s",
        "wifi.ap_bssid": "%s",
        "wifi.ip_addr": "%s",
        "wifi.gw_addr": "%s",
        "wifi.nm_addr": "%s",
        "sntp.last_sync_time": %)" PRIu64 R"(,
        "sntp.sync_duration_ms": %)" PRIu32 R"(,
        "mqtt.connect_attempt_count": %)" PRIu32 R"(,
        "mqtt.disconnected_count": %)" PRIu32 R"(,
        "mqtt.connected_count": %)" PRIu32 R"(,
        "mqtt.connect_duration_ms": %)" PRIu32 R"(,
        "mqtt.tx_pkt_count": %)" PRIu32 R"(,
        "mqtt.tx_bytes_count": %)" PRIu64 R"(,
        "mqtt.rx_pkt_count": %)" PRIu32 R"(,
        "mqtt.rx_bytes_count": %)" PRIu64 R"(
        }
      })";

  time_t report_id = time(nullptr);
  auto wifi_metrics = wifi_connect_get_metrics();
  auto mqtt_metrics = mqtt_client_get_metrics();
  auto sntp_metrics = sntp_sync_get_metrics();

  size_t len = snprintf(buffer, max_len, metrics_format,
                        (uint32_t) report_id,
                        s_device_metrics.boot_count, s_device_metrics.crash_count, s_device_metrics.last_crash_reason,
                        (uint32_t)esp_get_free_heap_size(),
                        (uint32_t)esp_get_minimum_free_heap_size(),
                        wifi_metrics.connect_attempt_count, wifi_metrics.disconnected_count,
                        wifi_metrics.connected_count, wifi_metrics.connect_duration_ms, wifi_metrics.rssi,
                        wifi_metrics.channel,
                        wifi_metrics.ssid, wifi_metrics.ap_bssid, wifi_metrics.ip_addr, wifi_metrics.gw_addr,
                        wifi_metrics.nm_addr,
                        (uint64_t) sntp_metrics.last_sync_time, sntp_metrics.sync_duration_ms,
                        mqtt_metrics.connect_attempt_count, mqtt_metrics.disconnected_count,
                        mqtt_metrics.connected_count,
                        mqtt_metrics.connect_duration_ms,
                        mqtt_metrics.tx_pkt_count, mqtt_metrics.tx_bytes_count,
                        mqtt_metrics.rx_pkt_count, mqtt_metrics.rx_bytes_count);

  _last_report_time = report_id;
  ESP_LOGI(TAG, "%.*s\n", len, buffer);

  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = metrics_topic,
      .topicNameLength = (uint16_t) strlen(metrics_topic),
      .pPayload = buffer,
      .payloadLength = len,
  };

  // Send as best effort, not fussed
  mqtt_client_publish(&publishInfo, 0);

}

bool app_metrics_update_required(int interval_sec) {
  return (time(nullptr) - _last_report_time) > (interval_sec);
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    // Refresh metrics on new connection
    _last_report_time = 0;
  }
}

void app_metrics_init() {
  _record_metrics();

  // Regular telemetry
  sprintf(metrics_topic, "%s/%s/telemetry/metrics", CMAKE_THING_TYPE, identity_thing_id());
  // Register connect events so we can send shadow on connect
  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr));

  ESP_LOGI(TAG, " >>>>>>>>>>>>>>>>>> Boot count: %" PRIu32 "\n", s_device_metrics.boot_count);
}