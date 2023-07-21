#pragma once


#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "core_mqtt_serializer.h"

struct mqtt_metrics_t {
  uint32_t connect_attempt_count;
  uint32_t connected_count;
  uint32_t disconnected_count;
  uint32_t connect_duration_ms;
  uint32_t tx_pkt_count;
  uint64_t tx_bytes_count;
  uint32_t rx_pkt_count;
  uint64_t rx_bytes_count;
};

int mqtt_client_subscribe( const MQTTSubscribeInfo_t* topics, size_t nunTopics, uint16_t ackWaitMS );

int mqtt_client_unsubscribe( const MQTTSubscribeInfo_t* topics, size_t nunTopics, uint16_t ackWaitMS );

int mqtt_client_publish(const MQTTPublishInfo_t* publishInfo, uint16_t ackWaitMS);

esp_err_t mqtt_client_disconnect();

esp_err_t mqtt_client_init(EventGroupHandle_t networkEventGroup);

mqtt_metrics_t mqtt_client_get_metrics();
