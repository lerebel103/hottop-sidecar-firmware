#include "shadow_handler.h"
#include "common/events_common.h"
#include "core_mqtt_serializer.h"
#include "core_mqtt.h"
#include "mqtt/mqtt_client.h"
#include "common/identity.h"
#include "fleet_provisioning/mqtt_provision.h"
#include <esp_err.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <esp_event.h>

#define TAG "shadow"

struct device_shadow_t {
  device_shadow_cfg_t cfg;

  char *topic_get_pub;
  char *topic_update_pub;
  char *topic_get_sub;
  char *topic_update_sub;
  char *topic_delete_sub;
};

void null_shadow_handler(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Receive into NULL shadow handler, topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGI(TAG, "Payload ps %.*s",
           pxPublishInfo->payloadLength, (const char *) pxPublishInfo->pPayload);
}


void _allocate_topic(device_shadow_cfg_t cfg, char **dest, const char *command, const char *suffix) {
  if (strlen(suffix) == 0) {
    if (strlen(cfg.name) == 0) {
      asprintf(dest, "$aws/things/%s/shadow/%s", identity_thing_id(), command);
    } else {
      asprintf(dest, "$aws/things/%s/shadow/name/%s/%s", identity_thing_id(), cfg.name, command);
    }
  } else {
    if (strlen(cfg.name) == 0) {
      asprintf(dest, "$aws/things/%s/shadow/%s/%s", identity_thing_id(), command, suffix);
    } else {
      asprintf(dest, "$aws/things/%s/shadow/name/%s/%s/%s", identity_thing_id(), cfg.name, command, suffix);
    }
  }
}

esp_err_t _subscribe(device_shadow_t *handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "shadow handler instance is null");
  esp_err_t ret = ESP_OK;

  ESP_LOGI(TAG, "Subscribing to shadow topics");
  static const int NUM_SUBSCRIPTIONS = 3;
  MQTTSubscribeInfo_t subscribeInfo[NUM_SUBSCRIPTIONS] = {
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = handle->topic_get_sub,
          .topicFilterLength = (uint16_t) strlen(handle->topic_get_sub)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = handle->topic_update_sub,
          .topicFilterLength = (uint16_t) strlen(handle->topic_update_sub)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = handle->topic_delete_sub,
          .topicFilterLength = (uint16_t) strlen(handle->topic_delete_sub)
      },
  };

  // Nothing will work if we don't have subscriptions,
  // this will block the event queue but for good reasons
  mqtt_client_subscribe(subscribeInfo, NUM_SUBSCRIPTIONS, UINT16_MAX);

  // Register callbacks
  SubscriptionManager_RegisterCallback(handle->topic_get_sub, strlen(handle->topic_get_sub), handle->cfg.get);
  SubscriptionManager_RegisterCallback(handle->topic_update_sub, strlen(handle->topic_update_sub), handle->cfg.updated);
  SubscriptionManager_RegisterCallback(handle->topic_delete_sub, strlen(handle->topic_delete_sub), handle->cfg.deleted);

  return ret;
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT) {
    if (!mqtt_provisioning_active()) {
      // Only do shadows when we are not provisioning
      auto *handle = (device_shadow_t *) arg;
      _subscribe(handle);

      // Now fetch shadow
      shadow_handler_get(handle);
    }
  }
}

esp_err_t shadow_handler_init(struct device_shadow_cfg_t cfg, device_shadow_handle_t *ret_handle) {
  esp_err_t ret = ESP_OK;

  device_shadow_t *handle;
  ESP_GOTO_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
  handle = (device_shadow_t *) heap_caps_calloc(1, sizeof(device_shadow_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG, "no memory left for allocation");

  // Copy conf across
  ESP_GOTO_ON_FALSE(cfg.get, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.get");
  ESP_GOTO_ON_FALSE(cfg.updated, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.updated");
  ESP_GOTO_ON_FALSE(cfg.deleted, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.deleted");
  memcpy(&handle->cfg, &cfg, sizeof(device_shadow_cfg_t));

  // listen to connection events, so we can setup subscriptions
  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, handle));

  // Create publish topic
  _allocate_topic(handle->cfg, &handle->topic_get_pub, "get", "");
  _allocate_topic(handle->cfg, &handle->topic_update_pub, "update", "");
  _allocate_topic(handle->cfg, &handle->topic_get_sub, "get", "#");
  _allocate_topic(handle->cfg, &handle->topic_update_sub, "update", "#");
  _allocate_topic(handle->cfg, &handle->topic_delete_sub, "delete", "#");

  // Good to go
  if (strlen(cfg.name) > 0) {
    ESP_LOGI(TAG, "Shadow handler created for named shadow '%s'", handle->cfg.name);
  } else {
    ESP_LOGI(TAG, "Shadow handler created for Classic shadow");
  }
  *ret_handle = handle;
  return ret;

  err:
  if (ret_handle) {
    return shadow_handler_del(handle);
  }
  return ret;
}

esp_err_t shadow_handler_get(device_shadow_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "shadow handler instance is null");

  // Publish blank message to get entire shadow
  const static char *payload = "{}";
  MQTTPublishInfo_t publishInfo = {
      .qos = MQTTQoS_t::MQTTQoS1,
      .retain = false,
      .dup = false,
      .pTopicName = handle->topic_get_pub,
      .topicNameLength = (uint16_t) strlen(handle->topic_get_pub),
      .pPayload = payload,
      .payloadLength = strlen(payload),
  };

  int status = mqtt_client_publish(&publishInfo, UINT16_MAX);
  return status == EXIT_SUCCESS ? ESP_OK : ESP_FAIL;
}

esp_err_t shadow_handler_update(device_shadow_handle_t handle, bool wait) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "shadow handler instance is null");
  esp_err_t ret = ESP_OK;

  return ret;

}

esp_err_t shadow_handler_del(device_shadow_handle_t handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
  if (strlen(handle->cfg.name) > 0) {
    ESP_LOGI(TAG, "Deleting Shadow handler created for named shadow '%s'", handle->cfg.name);
  } else {
    ESP_LOGI(TAG, "Deleting Shadow handler created for Classic shadow");
  }

  // free memory resource
  free(handle->topic_get_pub);
  free(handle->topic_update_pub);
  free(handle->topic_get_sub);
  free(handle->topic_update_sub);
  free(handle->topic_delete_sub);
  free(handle);
  return ESP_OK;
}
