#include <esp_event.h>
#include <cstring>
#include <cJSON.h>
#include "app_config.h"
#include "shadow/shadow_handler.h"
#include "common/events_common.h"
#include "fleet_provisioning/mqtt_provision.h"
#include "control_loop.h"
#include "telemetry.h"

#define TAG "app_config"

static device_shadow_handle_t shadow_handle;
static bool _update_required = false;
static bool _delete_control_required = false;
static bool _delete_telemetry_required = false;

static void _delete_desired(const char* item) {
  static const char *format =
      R"({
      "state": {
        "desired": {
          "%s": null
          }
        }
      })";

  static char payload[512];
  size_t len = sprintf(payload, format, item);
  shadow_handler_update(shadow_handle, payload, len);
}

void app_config_update_send(char* payload, size_t max_len) {
  static const char *format =
      R"({
      "state": {
        "reported": {
          "control": {
            "max_heat_ratio": %f,
            "max_tc_temp": %d,
            "max_board_temp": %d,
            "mains_hz": %d
            },
            "telemetry": {
              "status_interval": %d,
              "metrics_interval": %d
            }
          }
        }
      })";

  auto controller_cfg = controller_get_cfg();
  auto telemetry_cfg = telemetry_get_cfg();
  size_t len = snprintf(payload, max_len, format,
                       controller_cfg.max_heat_ratio,
                       controller_cfg.max_tc_temp,
                       controller_cfg.max_board_temp,
                       controller_cfg.mains_hz,
                       telemetry_cfg.status_interval_s,
                       telemetry_cfg.metrics_interval_s);

  ESP_LOGI(TAG, "%.*s\n", len, payload);
  shadow_handler_update(shadow_handle, payload, len);

  // Also run deletes as required
  if (_delete_telemetry_required) {
    _delete_desired("telemetry");
    _delete_telemetry_required = false;
  }

  if (_delete_control_required) {
    _delete_desired("control");
    _delete_control_required = false;
  }

  _update_required = false;
}

void _get_handler(MQTTContext_t *ctx, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGD(TAG, "Receive shadow topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGD(TAG, "Shadow ps %.*s",
           pxPublishInfo->payloadLength, (const char *) pxPublishInfo->pPayload);
}

static esp_err_t _update_controls(cJSON* json) {
  control_cfg_t cfg = controller_get_cfg();

  cJSON* item = cJSON_GetObjectItem(json, "max_heat_ratio");
  if (item != nullptr) {
    cfg.max_heat_ratio = item->valuedouble;
  }

  item = cJSON_GetObjectItem(json, "max_tc_temp");
  if (item != nullptr) {
    cfg.max_tc_temp = item->valueint;
  }

  item = cJSON_GetObjectItem(json, "max_board_temp");
  if (item != nullptr) {
    cfg.max_board_temp = item->valueint;
  }

  item = cJSON_GetObjectItem(json, "mains_hz");
  if (item != nullptr) {
    cfg.mains_hz = item->valueint;
  }

  return controller_set_cfg(cfg);
}

static esp_err_t _update_metrics(cJSON* json) {
  telemetry_cfg_t cfg = telemetry_get_cfg();
  cJSON* item;

  item = cJSON_GetObjectItem(json, "status_interval");
  if (item != nullptr) {
    cfg.status_interval_s = item->valueint;
  }

  item = cJSON_GetObjectItem(json, "metrics_interval");
  if (item != nullptr) {
    cfg.metrics_interval_s = item->valueint;
  }

  return telemetry_set_cfg(cfg);
}

void _updated_handler(MQTTContext_t *ctx, MQTTPublishInfo_t *pxPublishInfo) {

  // Make sure it is a delta
  if(strnstr((const char *) pxPublishInfo->pTopicName, "update/delta", pxPublishInfo->topicNameLength)) {

    ESP_LOGI(TAG, ">>> Receive shadow delta %.*s", pxPublishInfo->payloadLength,
             (const char *) pxPublishInfo->pPayload);
    cJSON* item, *items;
    cJSON *root = cJSON_ParseWithLength((const char *) pxPublishInfo->pPayload, pxPublishInfo->payloadLength);
    if (root == nullptr) {
      ESP_LOGE(TAG, "Failed to parse JSON");
      goto error;
    }

    items = cJSON_GetObjectItem(root, "state");
    if (items == nullptr) {
      ESP_LOGE(TAG, "Failed to get state");
      goto error;
    }

    cJSON_ArrayForEach(item, items) {
      if (strcmp(item->string, "control") == 0) {
        auto ret = _update_controls(item);
        if (ret == ESP_FAIL) {
          // delete item and set to null then, it is invalid
          _delete_control_required = true;
        }
      } else if (strcmp(item->string, "telemetry") == 0) {
        auto ret = _update_metrics(item);
        if (ret == ESP_FAIL) {
          // delete item and set to null then, it is invalid
          _delete_telemetry_required = true;
        }
      }
    }

    if (root) {
      cJSON_Delete(root);
    }

    // Set flag now
    _update_required = true;
    return;

    error:
    if (root) {
      cJSON_Delete(root);
    }
  }
}

static void _deleted_handler(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo) {
  // re-create the shadow then
  _update_required = true;
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT && !mqtt_provisioning_active()) {
    // Refresh metrics on new connection
    _update_required = true;
  }
}

bool app_config_update_required() {
  return _update_required;
}

void app_config_init() {
  device_shadow_cfg_t shadow_cfg = {.name = "config", .get = _get_handler, .updated = _updated_handler, .deleted = _deleted_handler};
  ESP_ERROR_CHECK(shadow_handler_init(shadow_cfg, &shadow_handle));

  // Register connect events so we can send shadow on connect
  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr));
}