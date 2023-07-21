#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "wifi_connect.h"
#include "qrcode.h"
#include "sntp/sntp_sync.h"
#include "common/events_common.h"
#include "common/identity.h"
#include <esp_netif.h>
#include <wifi_provisioning/manager.h>
#include <esp_wifi_default.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <wifi_provisioning/scheme_ble.h>
#include <esp_mac.h>

#define TAG "wifi_connect"

#define CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE  1
#define CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT     3

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_BLE      "ble"


/**
 * @brief The event group used to manage network events.
 */
static EventGroupHandle_t xNetworkEventGroup;
static wifi_metrics_t s_metrics = {};
static int64_t _start_time;

static char *_get_short_mac() {
  static char mac[14];
  uint8_t l_Mac[6];
  esp_efuse_mac_get_default(l_Mac);
  snprintf(mac, 14, "%02hX%02hX%02hX%02hX%02hX%02hX",
           l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3], l_Mac[4], l_Mac[5]);
  return mac;
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
  static int retries;
#endif
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
      case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        break;
      case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data;
        ESP_LOGI(TAG, "Received Wi-Fi credentials"
                      "\n\tSSID     : %s\n", (const char *) wifi_sta_cfg->ssid);
        break;
      }
      case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *) event_data;
        ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                      "\n\tPlease reset to factory and retry provisioning",
                 (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                 "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
        retries++;
        if (retries >= CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT) {
          ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
          wifi_prov_mgr_reset_sm_state_on_failure();
          retries = 0;
        }
#endif
        break;
      }
      case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful");
#ifdef CONFIG_EXAMPLE_RESET_PROV_MGR_ON_FAILURE
        retries = 0;
#endif
        break;
      case WIFI_PROV_END:
        /* De-initialize manager once provisioning is finished */
        ESP_LOGI(TAG, "Provisioning completed, stopping bluetooth");
        wifi_prov_mgr_deinit();
        xEventGroupSetBits(xNetworkEventGroup, WIFI_PROVISIONED_BIT);
        break;
      default:
        break;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    s_metrics.connect_attempt_count++;
    _start_time = esp_timer_get_time();
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    sprintf(s_metrics.ip_addr, IPSTR, IP2STR(&event->ip_info.ip));
    sprintf(s_metrics.gw_addr, IPSTR, IP2STR(&event->ip_info.gw));
    sprintf(s_metrics.nm_addr, IPSTR, IP2STR(&event->ip_info.netmask));
    ESP_LOGI(TAG, "Connected with IP %s, GW %s, NM %s", s_metrics.ip_addr, s_metrics.gw_addr, s_metrics.nm_addr);

    // Set host name as STA client
    char hostname[128];
    char* mac = _get_short_mac();
    sprintf(hostname, "%s-%s", identity_get()->thing_type, mac);
    ESP_ERROR_CHECK(esp_netif_set_hostname(event->esp_netif, hostname));
    ESP_LOGI(TAG, "Hostname set to  %s", hostname);

    /* Signal main application to continue execution */
    xEventGroupSetBits(xNetworkEventGroup, WIFI_CONNECTED_BIT);

    // Start SNTP
    sntp_sync_init(xNetworkEventGroup);
    s_metrics.connected_count++;
    s_metrics.connect_duration_ms = (esp_timer_get_time() - _start_time) / 1000;
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
    xEventGroupClearBits(xNetworkEventGroup, WIFI_CONNECTED_BIT);
    esp_wifi_connect();
    s_metrics.disconnected_count++;
    _start_time = esp_timer_get_time();
  }
}


static void wifi_init_sta(void) {
  /* Start Wi-Fi in station mode */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, 60));

  wifi_config_t cfg;
  esp_wifi_get_config(WIFI_IF_STA, &cfg);
  cfg.sta.listen_interval = 16;
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
}

static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "PROV_";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X",
           ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}


/* static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
  if (inbuf) {
    ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *) inbuf);
  }

  return ESP_OK;
} */

static void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport) {
  if (!name || !transport) {
    ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
    return;
  }
  char payload[150] = {0};
  if (pop) {
    snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                    ",\"pop\":\"%s\",\"transport\":\"%s\"}",
             PROV_QR_VERSION, name, pop, transport);
  }
  ESP_LOGI(TAG, "Scan this QR code from the provisioning application for Provisioning.");
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  esp_qrcode_generate(&cfg, payload);
}

static void revstr(uint8_t *str1, size_t len)
{
  uint8_t temp;
  for (int i = 0; i < len/2; i++)
  {
    temp = str1[i];
    str1[i] = str1[len - i - 1];
    str1[len - i - 1] = temp;
  }
}

wifi_metrics_t wifi_connect_get_metrics() {
  // Populate metrics we haven't yet populated
  wifi_ap_record_t record;
  esp_wifi_sta_get_ap_info(&record);
  sprintf(s_metrics.ssid, "%s", record.ssid);
  sprintf(s_metrics.ap_bssid, "%02hx:%02hx:%02hx:%02hx:%02hx:%02hx", record.bssid[0], record.bssid[1], record.bssid[2],
          record.bssid[3], record.bssid[4], record.bssid[5]);
  s_metrics.channel = record.primary;
  s_metrics.rssi = record.rssi;

  return s_metrics;
}


void wifi_connect_init(EventGroupHandle_t networkEventGroup) {
  xNetworkEventGroup = networkEventGroup;

  /* Initialize TCP/IP */
  ESP_ERROR_CHECK(esp_netif_init());

  /* Register our event handler for Wi-Fi, IP and Provisioning related events */
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  /* Initialize Wi-Fi including netif with default config */
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Let's find out if the device is provisioned */
  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

  /* If device is not yet provisioned start provisioning service */
  if (CONFIG_BLE_WIFI_PROV_ENABLED && !provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");
    xEventGroupClearBits(xNetworkEventGroup, WIFI_PROVISIONED_BIT);

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));

    uint8_t custom_service_uuid[16] = { };
    uint64_t value = CONFIG_BLE_WIFI_PROV_CUSTOM_SERVICE_UUID_LOW;
    memcpy(&custom_service_uuid[0], &value, 8);
    value = CONFIG_BLE_WIFI_PROV_CUSTOM_SERVICE_UUID_HIGH;
    memcpy(&custom_service_uuid[8], &value, 8);
    revstr(custom_service_uuid, 16);
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    // wifi_prov_mgr_endpoint_create("custom-data");

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;

    /* Proof of possession is short mac */
    const char *pop = _get_short_mac();

    /* This is the structure for passing security parameters
     * for the protocomm security 1.
     */
    wifi_prov_security1_params_t *sec_params = pop;

    const char *username = NULL;
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, NULL));
    // wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
    wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_BLE);

  } else {
    // Already provisioned
    if(CONFIG_BLE_WIFI_PROV_ENABLED) {
      ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
      xEventGroupSetBits(xNetworkEventGroup, WIFI_PROVISIONED_BIT);
    }

    /* Start Wi-Fi station */
    wifi_init_sta();
  }
}
