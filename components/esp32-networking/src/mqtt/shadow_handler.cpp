#include "shadow_handler.h"
#include "events_common.h"
#include "mqtt.h"
#include "core_mqtt_serializer.h"
#include <esp_err.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <esp_event.h>

#define TAG "shadow"

struct device_shadow_t {
  device_shadow_cfg_t cfg;

  bool is_subscribed = false;
  char* topic_get;
  char* topic_update;
  char* topic_get_accepted;
  char* topic_get_rejected;
  char* topic_update_accepted;
  char* topic_update_rejected;
  char* topic_delete_accepted;
  char* topic_delete_rejected;

};

void null_shadow_handler(void *pvIncomingPublishCallbackContext, MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Receive into NULL shadow handler, topic %.*s",
           pxPublishInfo->topicNameLength, pxPublishInfo->pTopicName);
  ESP_LOGI(TAG, "Payload ps %.*s",
           pxPublishInfo->payloadLength, (const char*)pxPublishInfo->pPayload);
}


void _allocate_topic(device_shadow_cfg_t cfg, char** dest, const char* command, const char* suffix) {
  if (strlen(suffix) == 0) {
    if (strlen(cfg.name) == 0) {
      asprintf(dest, "$aws/things/%s/shadow/%s", mqtt_thing_id(), command);
    } else {
      asprintf(dest, "$aws/things/%s/shadow/name/%s/%s", mqtt_thing_id(), cfg.name, command);
    }
  } else  {
    if (strlen(cfg.name) == 0) {
      asprintf(dest, "$aws/things/%s/shadow/%s/%s", mqtt_thing_id(), command, suffix);
    } else {
      asprintf(dest, "$aws/things/%s/shadow/name/%s/%s/%s", mqtt_thing_id(), cfg.name, command, suffix);
    }
  }
}

esp_err_t _subscribe(device_shadow_t* handle) {
  ESP_RETURN_ON_FALSE(handle, ESP_ERR_INVALID_ARG, TAG, "shadow handler instance is null");
  if(handle->is_subscribed) {
    ESP_LOGI(TAG, "Already subscribed to shadow topic, skipping");
    return ESP_OK;
  }
  esp_err_t ret = ESP_OK;

  ESP_LOGI(TAG, "Subscribing to shadow topics");
  _allocate_topic(handle->cfg, &handle->topic_get_accepted, "get", "accepted");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_get_accepted, strlen(handle->topic_get_accepted), handle->cfg.get_accepted);
  _allocate_topic(handle->cfg, &handle->topic_get_rejected, "get", "rejected");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_get_rejected, strlen(handle->topic_get_rejected), handle->cfg.get_rejected);

  _allocate_topic(handle->cfg, &handle->topic_update_accepted, "update", "accepted");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_update_accepted, strlen(handle->topic_update_accepted), handle->cfg.update_accepted);
  _allocate_topic(handle->cfg, &handle->topic_update_rejected, "update", "rejected");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_update_rejected, strlen(handle->topic_update_rejected), handle->cfg.update_rejected);

  _allocate_topic(handle->cfg, &handle->topic_delete_accepted, "delete", "accepted");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_delete_accepted, strlen(handle->topic_delete_accepted), handle->cfg.delete_accepted);
  _allocate_topic(handle->cfg, &handle->topic_delete_rejected, "delete", "rejected");
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, handle->topic_delete_rejected, strlen(handle->topic_delete_rejected), handle->cfg.delete_rejected);

  handle->is_subscribed = true;
  return ret;
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_AGENT_CONNECTED_EVENT) {
    if (!mqtt_is_provisioning()) {
      // Only do shadows when we are not provisioning
      auto *handle = (device_shadow_t *)arg;
      _subscribe(handle);

      // Now fetch shadow
      shadow_handler_get(handle);
    }
  }
}

esp_err_t shadow_handler_init(struct device_shadow_cfg_t cfg, device_shadow_handle_t* ret_handle) {
  esp_err_t ret = ESP_OK;

  device_shadow_t *handle;
  ESP_GOTO_ON_FALSE(ret_handle, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
  handle = (device_shadow_t *) heap_caps_calloc(1, sizeof(device_shadow_t), MALLOC_CAP_DEFAULT);
  ESP_GOTO_ON_FALSE(handle, ESP_ERR_NO_MEM, err, TAG, "no memory left for allocation");

  // Copy conf across
  ESP_GOTO_ON_FALSE(cfg.get_accepted, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.get_accepted");
  ESP_GOTO_ON_FALSE(cfg.get_rejected, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.get_rejected");
  ESP_GOTO_ON_FALSE(cfg.update_accepted, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.update_accepted");
  ESP_GOTO_ON_FALSE(cfg.update_rejected, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.update_rejected");
  ESP_GOTO_ON_FALSE(cfg.delete_accepted, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.delete_accepted");
  ESP_GOTO_ON_FALSE(cfg.delete_rejected, ESP_ERR_INVALID_ARG, err, TAG, "invalid handler for cfg.delete_rejected");
  memcpy(&handle->cfg, &cfg, sizeof (device_shadow_cfg_t));

  // listen to connection events, so we can setup subscriptions
  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_AGENT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, handle));

  // Create publish topic
  _allocate_topic(handle->cfg, &handle->topic_get, "get", "");
  _allocate_topic(handle->cfg, &handle->topic_update, "update", "");

  printf("%s\n", handle->topic_get);

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
  esp_err_t ret = ESP_OK;

  // Publish blank message to get entire shadow
  const static char *payload = "{}";
  mqttPublishMessage(handle->topic_get, strlen(handle->topic_get), payload, strlen(payload), MQTTQoS1);

  return ret;
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
  free(handle->topic_get);
  free(handle->topic_update);

  free(handle->topic_get_accepted);
  free(handle->topic_get_rejected);
  free(handle->topic_update_accepted);
  free(handle->topic_update_rejected);
  free(handle->topic_delete_accepted);
  free(handle->topic_delete_rejected);
  free(handle);
  return ESP_OK;
}
