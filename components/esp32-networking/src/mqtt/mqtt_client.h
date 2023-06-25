#pragma once


#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "core_mqtt_serializer.h"


int mqtt_client_subscribe( const MQTTSubscribeInfo_t* topics, size_t nunTopics, uint16_t ackWaitMS );

int mqtt_client_unsubscribe( const MQTTSubscribeInfo_t* topics, size_t nunTopics, uint16_t ackWaitMS );

int mqtt_client_publish(const MQTTPublishInfo_t* publishInfo, uint16_t ackWaitMS);

esp_err_t mqtt_client_disconnect();

esp_err_t mqtt_client_init(EventGroupHandle_t networkEventGroup);
