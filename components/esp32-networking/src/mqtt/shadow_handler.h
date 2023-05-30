#pragma once

#include <esp_err.h>
#include "core_mqtt_serializer.h"

extern "C" {
  #include "pub_sub_manager.h"
}

/**
 * Handle for an instance of wave generator
 */
typedef struct device_shadow_t *device_shadow_handle_t;

void null_shadow_handler(void *pvIncomingPublishCallbackContext, MQTTPublishInfo_t *pxPublishInfo);


struct device_shadow_cfg_t {
  /**
   * Name of the shadow (zero length string for classic shadows)
   */
  char name[64];

  IncomingPubCallback_t get_accepted;
  IncomingPubCallback_t get_rejected = null_shadow_handler;
  IncomingPubCallback_t update_accepted = null_shadow_handler;
  IncomingPubCallback_t update_rejected = null_shadow_handler;
  IncomingPubCallback_t delete_accepted = null_shadow_handler;
  IncomingPubCallback_t delete_rejected = null_shadow_handler;
};

esp_err_t shadow_handler_init(struct device_shadow_cfg_t cfg, device_shadow_handle_t* ret_handle);
esp_err_t shadow_handler_get(device_shadow_handle_t handle);
esp_err_t shadow_handler_update(device_shadow_handle_t handle);
esp_err_t shadow_handler_del(device_shadow_handle_t handle);
