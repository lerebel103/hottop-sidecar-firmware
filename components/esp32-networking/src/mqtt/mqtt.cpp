#include "freertos/FreeRTOS.h"
#include "mqtt.h"

#include <string>
#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/event_groups.h>
#include <cstring>
#include <freertos/semphr.h>
#include <esp_mac.h>
#include "transport_interface.h"
#include "core_mqtt_agent.h"
/* coreJSON include. */
#include "core_json.h"
#include "events_common.h"

extern "C" {
#include "mqtt/core_mqtt_agent_manager.h"
#include "subscription_manager.h"
}

#define TAG "mqtt"
#define NVS_IDENTITY_NAMESPACE        "identity"

#define NVS_THING_TYPE_KEY            "thing_type"
#define NVS_STAGE_NAME_KEY            "stage_name"
#define NVS_PROV_TEMPLATE_KEY         "prov_templ"
#define NVS_ROTATE_TEMPLATE_KEY       "rotate_templ"
#define NVS_ATS_ENDPOINT_KEY          "ats_ep"
#define NVS_JOBS_ENDPOINT_KEY         "jobs_ep"
#define NVS_CA_CERT_KEY               "ca_cert"
#define NVS_PROV_CERT_KEY             "prov_cert"
#define NVS_PROV_PRIVATE_KEY_KEY      "prov_key"
#define NVS_DEVICE_CERT_KEY           "device_cert"
#define NVS_DEVICE_PRIVATE_KEY_KEY    "device_key"


/**
 * @brief ALPN (Application-Layer Protocol Negotiation) protocol name for AWS IoT MQTT.
 *
 * This will be used if the AWS_MQTT_PORT is configured as 443 for AWS IoT MQTT broker.
 * Please see more details about the ALPN protocol for AWS IoT MQTT endpoint
 * in the link below.
 * https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/
 */
#define AWS_IOT_MQTT_ALPN               "x-amzn-mqtt-ca"

struct identity_t {
  char *stage_name;
  char *thing_type;
  char *prov_template;
  char *rotate_template;
  char *ats_ep;
  char *jobs_ep;
  char *ca_cert;
  char *prov_cert;
  char *prov_private_key;
  char *device_cert;
  char *device_private_key;
};

static char g_macStr[32] = {0};

static identity_t s_identity;
static NetworkContext_t xNetworkContext;
static EventGroupHandle_t s_networkEventGroup;

static char *new_credentials_payload = nullptr;
static bool s_is_provisioning = false;
static bool s_topics_subscribed = false;

/**
 * @brief Static handle used for MQTT agent context.
 */
extern MQTTAgentContext_t xGlobalMqttAgentContext;

ESP_EVENT_DEFINE_BASE(MQTT_PROVISIONING_EVENT);


const char *mqtt_thing_id() {
  if (strlen(g_macStr) == 0) {
    uint8_t l_Mac[6];
    esp_efuse_mac_get_default(l_Mac);
    snprintf(g_macStr, sizeof(g_macStr), "%02hx:%02hx:%02hx:%02hx:%02hx:%02hx",
             l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3], l_Mac[4], l_Mac[5]);
  }
  return g_macStr;
}

static BaseType_t prvInitializeNetworkContext(void) {
  /* This is returned by this function. */
  BaseType_t xRet = pdPASS;

  /* Verify that the MQTT endpoint and thing name have been configured by the
   * user. */
  if (strlen(s_identity.ats_ep) == 0) {
    ESP_LOGE(TAG, "Empty endpoint for MQTT broker.");
    xRet = pdFAIL;
  }

  if (strlen(mqtt_thing_id()) == 0) {
    ESP_LOGE(TAG, "Empty thingname for MQTT broker.");
    xRet = pdFAIL;
  }

  /* Initialize network context. */

  xNetworkContext.pcHostname = s_identity.ats_ep;
  xNetworkContext.xPort = 443;

  /* Initialize credentials for establishing TLS session. */
  xNetworkContext.pcServerRootCA = s_identity.ca_cert;
  xNetworkContext.pcServerRootCASize = strlen(s_identity.ca_cert) + 1;

  char *cert = s_identity.device_cert;
  char *private_key = s_identity.device_private_key;
  if (cert == nullptr || private_key == nullptr) {
    ESP_LOGW(TAG, "No client credentials, loading provisioning certificate");
    cert = s_identity.prov_cert;
    private_key = s_identity.prov_private_key;
    s_is_provisioning = true;
  }

  xNetworkContext.pcClientCert = cert;
  xNetworkContext.pcClientCertSize = strlen(cert) + 1;
  xNetworkContext.pcClientKey = private_key;
  xNetworkContext.pcClientKeySize = strlen(private_key) + 1;
  xNetworkContext.pxTls = NULL;
  xNetworkContext.xTlsContextSemaphore = xSemaphoreCreateMutex();

  /* AWS IoT requires devices to send the Server Name Indication (SNI)
   * extension to the Transport Layer Security (TLS) protocol and provide
   * the complete endpoint address in the host_name field. Details about
   * SNI for AWS IoT can be found in the link below.
   * https://docs.aws.amazon.com/iot/latest/developerguide/transport-security.html */

  static const char *pcAlpnProtocols[] = {NULL, NULL};
  pcAlpnProtocols[0] = AWS_IOT_MQTT_ALPN;
  xNetworkContext.pAlpnProtos = pcAlpnProtocols;

  if (xNetworkContext.xTlsContextSemaphore == NULL) {
    ESP_LOGE(TAG, "Not enough memory to create TLS semaphore for global network context.");
    xRet = pdFAIL;
  }

  return xRet;
}


static void _load_client_credentials(nvs_handle_t nvs_handle) {
  size_t len;

  // optional
  esp_err_t err = nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, nullptr, &len);
  if (err == ESP_OK) {
    s_identity.device_cert = (char *) calloc(len + 1, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, s_identity.device_cert, &len));
  }

  // optional
  err = nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, nullptr, &len);
  if (err == ESP_OK) {
    s_identity.device_private_key = (char *) calloc(len + 1, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, s_identity.device_private_key, &len));
  }
}

static void _load_identity() {
  nvs_handle_t nvs_handle;
  size_t len;
  ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, nullptr, &len));
  s_identity.thing_type = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, s_identity.thing_type, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_STAGE_NAME_KEY, nullptr, &len));
  s_identity.stage_name = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_STAGE_NAME_KEY, s_identity.stage_name, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_TEMPLATE_KEY, nullptr, &len));
  s_identity.prov_template = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_TEMPLATE_KEY, s_identity.prov_template, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ROTATE_TEMPLATE_KEY, nullptr, &len));
  s_identity.rotate_template = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ROTATE_TEMPLATE_KEY, s_identity.rotate_template, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, nullptr, &len));
  s_identity.ats_ep = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, s_identity.ats_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, nullptr, &len));
  s_identity.jobs_ep = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, s_identity.jobs_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, nullptr, &len));
  s_identity.ca_cert = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, s_identity.ca_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, nullptr, &len));
  s_identity.prov_cert = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, s_identity.prov_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, nullptr, &len));
  s_identity.prov_private_key = (char *) calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, s_identity.prov_private_key, &len));

  _load_client_credentials(nvs_handle);

  nvs_close(nvs_handle);
}

static void _save_str_to_nvs(const char *key, const char *outValue, uint32_t outValueLength, nvs_handle_t nvs_handle) {
  // This is a tad lazy using std::string
  std::string str(outValue, outValueLength);
  std::string::size_type index = 0;
  while ((index = str.find("\\n", index)) != std::string::npos) {
    str.replace(index, 2, "\n");
    ++index;
  }

  nvs_set_str(nvs_handle, key, str.c_str());
}


void _provision_accepted_handler(void *pvIncomingPublishCallbackContext,
                                 MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Provisioning accepted, applying new credentials");

  // Can't save data or change state of MQTT in this thread, got to be from main event loop
  ESP_ERROR_CHECK(esp_event_post(MQTT_PROVISIONING_EVENT,
                                 APPLY_CREDENTIALS,
                                 nullptr,
                                 0,
                                 portMAX_DELAY));
}

void _provision_rejected_handler(void *pvIncomingPublishCallbackContext,
                                 MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGE(TAG, "Provisioning rejected");
}

void _certificate_accepted_handler(void *pvIncomingPublishCallbackContext,
                                   MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Certificate received");
  // Make a copy of the data, so we can grab the token for registration, and handle storage post-registration
  asprintf(&new_credentials_payload, "%.*s", pxPublishInfo->payloadLength, (const char *) pxPublishInfo->pPayload);

  // We receive all items required
  if (s_is_provisioning) {
    ESP_ERROR_CHECK(esp_event_post(MQTT_PROVISIONING_EVENT,
                                   PROVISION_NEW_CERTIFICATE,
                                   nullptr,
                                   0,
                                   portMAX_DELAY));
  } else {
    ESP_ERROR_CHECK(esp_event_post(MQTT_PROVISIONING_EVENT,
                                   ROTATE_CERTIFICATE,
                                   nullptr,
                                   0,
                                   portMAX_DELAY));
  }
}

void _certificate_rejected_handler(void *pvIncomingPublishCallbackContext,
                                   MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGE(TAG, "Certificate rejected");
}


static void _subscribe_for_provisioning() {
  ESP_LOGI(TAG, "Subscribing for provisioning template '%s'", s_identity.prov_template);

  // Memory leaks here
  char *topic_prov_accepted;
  asprintf(&topic_prov_accepted, "$aws/provisioning-templates/%s/provision/json/accepted", s_identity.prov_template);
  const char *certificate_accepted_topic = "$aws/certificates/create/json/accepted";

  char *topic_prov_rejected;
  asprintf(&topic_prov_rejected, "$aws/provisioning-templates/%s/provision/json/rejected", s_identity.prov_template);
  const char *certificate_rejected_topic = "$aws/certificates/create/json/rejected";

  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, topic_prov_accepted, _provision_accepted_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, certificate_accepted_topic, _certificate_accepted_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, topic_prov_rejected, _provision_rejected_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, certificate_rejected_topic, _certificate_rejected_handler);
}

static void _subscribe_runtime_topics() {
  if(s_topics_subscribed) {
    ESP_LOGI(TAG, "Topics already subscribed");
  }

  ESP_LOGI(TAG, "Subscribing to runtime topics");

  // Memory leaks here
  char *topic_prov_accepted;
  asprintf(&topic_prov_accepted, "$aws/provisioning-templates/%s/provision/json/accepted", s_identity.rotate_template);
  const char *certificate_accepted_topic = "$aws/certificates/create/json/accepted";

  char *topic_prov_rejected;
  asprintf(&topic_prov_rejected, "$aws/provisioning-templates/%s/provision/json/rejected", s_identity.rotate_template);
  const char *certificate_rejected_topic = "$aws/certificates/create/json/rejected";

  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, topic_prov_accepted, _provision_accepted_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, certificate_accepted_topic, _certificate_accepted_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, topic_prov_rejected, _provision_rejected_handler);
  mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, certificate_rejected_topic, _certificate_rejected_handler);
}

static void _handle_apply_credentials() {
  assert(new_credentials_payload != nullptr);

  size_t len = strlen(new_credentials_payload);
  JSONStatus_t result = JSON_Validate(new_credentials_payload, len);
  if (result == JSONSuccess) {
    ESP_LOGI(TAG, "Processing payload to save credentials");
    uint32_t outValueLength = 0U;
    char *outValue;
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));

    result = JSON_Search((char *) new_credentials_payload,
                         len,
                         "certificatePem",
                         sizeof("certificatePem") - 1,
                         &outValue,
                         (size_t *) &outValueLength);
    if (result == JSONSuccess) {
      _save_str_to_nvs(NVS_DEVICE_CERT_KEY, outValue, outValueLength, nvs_handle);
    }

    result = JSON_Search((char *) new_credentials_payload,
                         len,
                         "privateKey",
                         sizeof("privateKey") - 1,
                         &outValue,
                         (size_t *) &outValueLength);
    if (result == JSONSuccess) {
      _save_str_to_nvs(NVS_DEVICE_PRIVATE_KEY_KEY, outValue, outValueLength, nvs_handle);
    }

    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);

    // And we can now free previously allocated credentials
    free(new_credentials_payload);
    new_credentials_payload = nullptr;

    // Restart if we were doing the initial provisioning to get a clean MQTT connection and set of subscriptions
    if (s_is_provisioning) {
      ESP_LOGI(TAG, "SUCCESS, restarting to load new credentials.");
      esp_restart();
    } else {
      // Install new certificate
      ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));
      _load_client_credentials(nvs_handle);
      nvs_close(nvs_handle);

      // Install new certs and carry on
      const char *old_cert = xNetworkContext.pcClientCert;
      const char *old_key = xNetworkContext.pcClientKey;

      xNetworkContext.pcClientCert = s_identity.device_cert;
      xNetworkContext.pcClientCertSize = strlen(s_identity.device_cert) + 1;
      xNetworkContext.pcClientKey = s_identity.device_private_key;
      xNetworkContext.pcClientKeySize = strlen(s_identity.device_private_key) + 1;

      // Need to stop MQTT client and restart it with new certificate in place
      xTlsDisconnect(&xNetworkContext);
      xEventGroupSetBits(s_networkEventGroup, CORE_MQTT_AGENT_DISCONNECTED_BIT);

      // Free old pointers
      free((char *) old_cert);
      free((char *) old_key);
    }

    // So we are good at this point
    ESP_LOGI(TAG, "SUCCESS, new credentials applied.");
  } else {
    ESP_LOGE(TAG, "The JSON document is invalid!");
    return;
  }
}

static void _handle_register_new_thing(const char *template_name) {
  ESP_LOGI(TAG, "Registering newly received credentials");
  size_t len = strlen(new_credentials_payload);
  JSONStatus_t result = JSON_Validate(new_credentials_payload, len);
  if (result == JSONSuccess) {
    uint32_t outValueLength = 0U;
    char *outValue;

    // Grap ownership token
    result = JSON_Search((char *) new_credentials_payload,
                         len,
                         "certificateOwnershipToken",
                         sizeof("certificateOwnershipToken") - 1,
                         &outValue,
                         (size_t *) &outValueLength);

    if (result == JSONSuccess) {
      // Cool, just need to register thing
      char *register_thing_payload;
      asprintf(&register_thing_payload,
               R"({"certificateOwnershipToken": "%.*s", "parameters": {"SerialNumber": "%s"}})",
               (uint16_t) outValueLength, outValue, mqtt_thing_id());

      char *topic;
      asprintf(&topic, "$aws/provisioning-templates/%s/provision/json", template_name);
      MQTTStatus_t status = mqttPublishMessage(topic, register_thing_payload, strlen(register_thing_payload), MQTTQoS1,
                                               true);
      if (status != MQTTSuccess) {
        ESP_LOGE(TAG, "Could not activate device");
      }
      free(topic);
      free(register_thing_payload);
    }
  } else {
    ESP_LOGE(TAG, "The JSON document is invalid!");
    return;
  }
}

static void agent_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_AGENT_CONNECTED_EVENT) {
    if (s_is_provisioning) {
      // Need to kick off request for new certificate by sending blank payload
      _subscribe_for_provisioning();
      const char *payload = "{}";
      mqttPublishMessage("$aws/certificates/create/json", payload, strlen(payload), MQTTQoS1, true);
    } else {
      // Normal flow then, might need to re-subscribe to expected topics
      _subscribe_runtime_topics();
    }
  } else {

  }
}

static void provisioning_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == PROVISION_NEW_CERTIFICATE) {
    const char *template_name = s_identity.prov_template;
    _handle_register_new_thing(template_name);
  } else if (event_id == ROTATE_CERTIFICATE) {
    const char *template_name = s_identity.rotate_template;
    _handle_register_new_thing(template_name);
  } else if (event_id == APPLY_CREDENTIALS) {
    _handle_apply_credentials();
  }
}

esp_err_t mqtt_init(EventGroupHandle_t networkEventGroup) {
  s_networkEventGroup = networkEventGroup;
  ESP_LOGI(TAG, "Init");

  if (s_identity.ats_ep != nullptr) {
    ESP_LOGE(TAG, "Already configured");
    return ESP_FAIL;
  }

  xEventGroupSetBits(networkEventGroup, CORE_MQTT_AGENT_OTA_NOT_IN_PROGRESS_BIT);
  _load_identity();

  ESP_LOGI(TAG, "Connecting to ATS endpoint: %s", s_identity.ats_ep);
  BaseType_t xResult = pdFAIL;

  xResult = prvInitializeNetworkContext();
  if (xResult != pdPASS) {
    ESP_LOGE(TAG, "Failed to initialize network context");
    configASSERT(xResult == pdPASS);
  }

  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_AGENT_EVENT, ESP_EVENT_ANY_ID, &agent_event_handler, NULL));
  ESP_ERROR_CHECK(
      esp_event_handler_register(MQTT_PROVISIONING_EVENT, ESP_EVENT_ANY_ID, &provisioning_event_handler, NULL));

  xResult = xCoreMqttAgentManagerStart(networkEventGroup, &xNetworkContext, mqtt_thing_id());
  if (xResult != pdPASS) {
    ESP_LOGE(TAG, "Failed to initialize and start coreMQTT-Agent network manager.");
    configASSERT(xResult == pdPASS);
  }

  mqttManagerPubSubInit(networkEventGroup);

  return ESP_OK;
}