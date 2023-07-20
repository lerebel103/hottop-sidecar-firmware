#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

struct network_metrics_t {
  uint32_t wifi_connect_attempt_count;
  uint32_t wifi_connected_count;
  uint32_t wifi_disconnected_count;
  uint32_t wifi_join_duration_ms;
  int16_t wifi_rssi;
  uint8_t wifi_channel;
  char wifi_ap_bssid[13];
  char wifi_ipv4_addr[16];
  char wifi_ipv6_addr[40];

  uint32_t sntp_sync_count;
  uint32_t sntp_sync_duration_ms;

  uint32_t mqtt_connect_attempt_count;
  uint32_t mqtt_connected_count;
  uint32_t mqtt_disconnected_count;
  uint32_t mqtt_connect_duration_ms;

};

void esp32_networking_init(EventGroupHandle_t net_group);
