#pragma once

#include "esp_event_base.h"

/* Network event group bit definitions */
#define WIFI_PROVISIONED_BIT                     ( 1 << 0 )
#define WIFI_CONNECTED_BIT                       ( 1 << 1 )
#define CORE_MQTT_CLIENT_CONNECTED_BIT           ( 1 << 2 )
#define SNTP_TIME_SYNCED_BIT                     ( 1 << 3 )
#define CORE_MQTT_OTA_IN_PROGRESS_BIT            ( 1 << 4 )


ESP_EVENT_DECLARE_BASE( CORE_MQTT_EVENT );
enum
{
  CORE_MQTT_CONNECTED_EVENT,
  CORE_MQTT_DISCONNECTED_EVENT,
  CORE_MQTT_OTA_STARTED_EVENT,
  CORE_MQTT_OTA_STOPPED_EVENT,
  CORE_MQTT_OTA_TRIGGER_RESTART,
};


ESP_EVENT_DECLARE_BASE( MQTT_PROVISIONING_EVENT );
enum {
  PROVISION_NEW_CERTIFICATE,
  ROTATE_CERTIFICATE,
  APPLY_CREDENTIALS
};
