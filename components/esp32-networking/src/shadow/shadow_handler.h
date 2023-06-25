#pragma once

#include <esp_err.h>
#include "core_mqtt_serializer.h"
#include "core_mqtt.h"
#include "mqtt/mqtt_subscription_manager.h"

/**
 * Handle for an instance of wave generator
 */
typedef struct device_shadow_t *device_shadow_handle_t;

void null_shadow_handler(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo);


struct device_shadow_cfg_t {
  /**
   * Name of the shadow (zero length string for classic shadows)
   */
  char name[64];

  SubscriptionManagerCallback_t get;
  SubscriptionManagerCallback_t updated = null_shadow_handler;
  SubscriptionManagerCallback_t deleted = null_shadow_handler;
};

esp_err_t shadow_handler_init(struct device_shadow_cfg_t cfg, device_shadow_handle_t* ret_handle);
esp_err_t shadow_handler_get(device_shadow_handle_t handle);
esp_err_t shadow_handler_update(device_shadow_handle_t handle);
esp_err_t shadow_handler_del(device_shadow_handle_t handle);
