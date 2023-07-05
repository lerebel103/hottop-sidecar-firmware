#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "wifi_connect.h"
#include "qrcode.h"
#include "sntp/sntp.h"
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
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"

static esp_netif_t *s_netif_sta;

/**
 * @brief The event group used to manage network events.
 */
static EventGroupHandle_t xNetworkEventGroup;


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
        wifi_prov_mgr_deinit();
        break;
      default:
        break;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

    // Set host name as STA client
    char hostname[128];
    char mac[14];
    uint8_t l_Mac[6];
    esp_efuse_mac_get_default(l_Mac);
    snprintf(mac, 14, "%02hX%02hX%02hX%02hX%02hX%02hX",
                    l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3], l_Mac[4], l_Mac[5]);
    sprintf(hostname, "%s-%s", identity_get()->thing_type, mac);
    ESP_ERROR_CHECK(esp_netif_set_hostname(event->esp_netif, hostname));
    ESP_LOGI(TAG, "Hostname set to  %s", hostname);

    /* Signal main application to continue execution */
    xEventGroupSetBits( xNetworkEventGroup,WIFI_CONNECTED_BIT );

    // Start SNTP
    sntp_sync_init(xNetworkEventGroup);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
    xEventGroupClearBits( xNetworkEventGroup,WIFI_CONNECTED_BIT );
    esp_wifi_connect();
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

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
  if (inbuf) {
    ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *) inbuf);
  }
  char response[] = "SUCCESS";
  *outbuf = (uint8_t *) strdup(response);
  if (*outbuf == NULL) {
    ESP_LOGE(TAG, "System out of memory");
    return ESP_ERR_NO_MEM;
  }
  *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

  return ESP_OK;
}

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

void wifi_connect_init(EventGroupHandle_t networkEventGroup) {
  xNetworkEventGroup = networkEventGroup;

  /* Initialize TCP/IP */
  ESP_ERROR_CHECK(esp_netif_init());

  /* Register our event handler for Wi-Fi, IP and Provisioning related events */
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

  /* Initialize Wi-Fi including netif with default config */
  s_netif_sta = esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* Configuration for the provisioning manager */
  wifi_prov_mgr_config_t config = {
      .scheme = wifi_prov_scheme_ble,
      .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
      .app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
  };

  /* Initialize provisioning manager with the
   * configuration parameters set above */
  ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

  /* Let's find out if the device is provisioned */
  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

  /* If device is not yet provisioned start provisioning service */
  if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");

    /* What is the Device Service Name that we want
     * This translates to :
     *     - Wi-Fi SSID when scheme is wifi_prov_scheme_softap
     *     - device name when scheme is wifi_prov_scheme_ble
     */
    char service_name[12];
    get_device_service_name(service_name, sizeof(service_name));

    /* This step is only useful when scheme is wifi_prov_scheme_ble. This will
 * set a custom 128 bit UUID which will be included in the BLE advertisement
 * and will correspond to the primary GATT service that provides provisioning
 * endpoints as GATT characteristics. Each GATT characteristic will be
 * formed using the primary service UUID as base, with different auto assigned
 * 12th and 13th bytes (assume counting starts from 0th byte). The client side
 * applications must identify the endpoints by reading the User Characteristic
 * Description descriptor (0x2901) for each characteristic, which contains the
 * endpoint name of the characteristic */
    uint8_t custom_service_uuid[] = {
        /* LSB <---------------------------------------
         * ---------------------------------------> MSB */
        0xfb, 0x41, 0x36, 0xd2, 0xf2, 0x4b, 0x11, 0xed,
        0xb2, 0xa5, 0xb6, 0x19, 0x10, 0x59, 0xc7, 0xc3,
    };

    /* If your build fails with linker errors at this point, then you may have
     * forgotten to enable the BT stack or BTDM BLE settings in the SDK (e.g. see
     * the sdkconfig.defaults in the example project) */
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);


    /* An optional endpoint that applications can create if they expect to
     * get some additional custom data during provisioning workflow.
     * The endpoint name can be anything of your choice.
     * This call must be made before starting the provisioning.
     */
    wifi_prov_mgr_endpoint_create("custom-data");
    /* Start provisioning service */

    /* What is the security level that we want (0, 1, 2):
 *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
 *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
 *          using X25519 key exchange and proof of possession (pop) and AES-CTR
 *          for encryption/decryption of messages.
 *      - WIFI_PROV_SECURITY_2 SRP6a based authentication and key exchange
 *        + AES-GCM encryption/decryption of messages
 */
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;

    /* Do we want a proof-of-possession (ignored if Security 0 is selected):
     *      - this should be a string with length > 0
     *      - NULL if not used
     */
    const char *pop = "abcd1234";

    /* This is the structure for passing security parameters
     * for the protocomm security 1.
     */
    wifi_prov_security1_params_t *sec_params = pop;

    const char *username  = NULL;

    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, NULL));

    /* The handler for the optional endpoint created above.
     * This call must be made after starting the provisioning, and only if the endpoint
     * has already been created above.
     */
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
    wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_BLE);
    // Flush output of console
    printf("\n\n\n\n");

    // Wait for service to complete
    wifi_prov_mgr_wait();

    // Finally de-initialize the manager
    wifi_prov_mgr_deinit();

    // Do a restart, the simplest approach
    esp_restart();
  } else {
    // ALready provisioned
    ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

    /* We don't need the manager as device is already provisioned,
     * so let's release its resources */
    wifi_prov_mgr_deinit();

    /* Start Wi-Fi station */
    wifi_init_sta();

  }
}
