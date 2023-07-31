#include "telemetry.h"
#include "core_mqtt_serializer.h"
#include "mqtt/mqtt_client.h"
#include "common/identity.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "defender.h"
#include "sntp/sntp_sync.h"
#include "app_metrics.h"
#include "device_info.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <ctime>
#include <nvs.h>
#include "control_loop.h"
#include "app_config.h"
#include "utils.h"

#define TAG "telemetry"
#define DEFAULT_STATUS_INTERVAL_SEC (5)
#define DEFAULT_METRICS_INTERVAL_SEC (60*30)

#define TOPIC_MAX_SIZE (128)
#define PAYLOAD_MAX_SIZE (1024)

static EventGroupHandle_t xNetworkEventGroup;
static bool _go = false;
static uint64_t _last_metrics_time = 0;

static telemetry_cfg_t s_cfg = {
    .status_interval_s = DEFAULT_STATUS_INTERVAL_SEC,
    .metrics_interval_s = DEFAULT_METRICS_INTERVAL_SEC,
};


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
  size_t len = sprintf(payload, format, (uint32_t)time(nullptr),
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
      .payloadLength = len,
  };

  ESP_LOGD(TAG, "%.*s\n", len, payload);
  mqtt_client_publish(&publishInfo, CONFIG_MQTT_ACK_TIMEOUT_MS);
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

      ESP_LOGD(TAG, "Sending payloads %" PRIu64, (now - _last_metrics_time));

      if (device_info_update_required()) {
        device_info_send(payload, PAYLOAD_MAX_SIZE);
      }
      if(app_config_update_required()) {
        app_config_update_send(payload, PAYLOAD_MAX_SIZE);
      }
      if (app_metrics_update_required(s_cfg.metrics_interval_s)) {
        app_metrics_send(payload, PAYLOAD_MAX_SIZE);
      }

      _send_status();
    }

    uint64_t delta = esp_timer_get_time() - now;
    auto interval = (uint64_t)(s_cfg.status_interval_s / 1e6);
    if (delta < interval) {
      vTaskDelay(pdMS_TO_TICKS((interval - delta))/1000);
    }
  } while (_go);
}

telemetry_cfg_t telemetry_get_cfg() {
  return s_cfg;
}

esp_err_t telemetry_set_cfg(telemetry_cfg_t cfg) {
  if (cfg.metrics_interval_s < 1 or cfg.metrics_interval_s > 3600*24) {
    ESP_LOGE(TAG, "Invalid metrics interval: %lu but must be [%d, %d]", cfg.metrics_interval_s, 1, 3600*24);
    goto error;
  }

  if (cfg.status_interval_s < 1 or cfg.status_interval_s > 3600*24) {
    ESP_LOGE(TAG, "Invalid status interval: %lu but must be [%d, %d]", cfg.status_interval_s, 1, 3600*24);
    goto error;
  }

  s_cfg = cfg;
  utils_save_to_nvs("telemetry", "cfg", &s_cfg, sizeof(telemetry_cfg_t));
  ESP_LOGI(TAG, "Set telemetry config: status_interval_s=%lu, metrics_interval_s=%lu",
           s_cfg.status_interval_s, s_cfg.metrics_interval_s);
  return ESP_OK;

  error:
  ESP_LOGE(TAG, "Failed to set telemetry config");
  return ESP_FAIL;
}



void telemetry_init(EventGroupHandle_t net_group) {
  if (_go) {
    return;
  }

  utils_load_from_nvs("telemetry", "cfg", &s_cfg, sizeof(telemetry_cfg_t));
  xNetworkEventGroup = net_group;
  device_info_init();
  app_metrics_init();

  // Regular telemetry
  sprintf(info_topic, "%s/%s/telemetry/status", CMAKE_THING_TYPE, identity_thing_id());

  _go = true;
  xTaskCreate(_send_telemetry, "send_telemetry", 3072, nullptr, 4, nullptr);
}
