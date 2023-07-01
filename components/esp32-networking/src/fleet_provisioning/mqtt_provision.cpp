#include <freertos/FreeRTOS.h>
#include "mqtt_provision.h"
#include "core_mqtt_serializer.h"
#include "events_common.h"
#include "common/identity.h"
#include "core_json.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_subscription_manager.h"

#include <string>
#include <esp_event.h>
#include <cstring>

#define TAG "provisioning"
#define MAX_PROV_TOPIC_LEN (128U)
#define NUM_SUBSCRIPTIONS  (4U)

static char *new_credentials_payload = nullptr;
static EventGroupHandle_t s_networkEventGroup;

// These need to be allocated for the life of the mqtt subscriptions
static char topic_prov_accepted[MAX_PROV_TOPIC_LEN];
static char topic_prov_rejected[MAX_PROV_TOPIC_LEN];
static const char *certificate_topic_accepted = "$aws/certificates/create/json/accepted";
static const char *certificate_topic_rejected = "$aws/certificates/create/json/rejected";


bool mqtt_provisioning_active() {
  return identity_get()->device_cert == nullptr;
}

void _provision_accepted_handler(MQTTContext_t *,
                                 MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Provisioning accepted, applying new credentials");

  // Can't save data or change state of MQTT in this thread, got to be from main event loop
  ESP_ERROR_CHECK(esp_event_post(MQTT_PROVISIONING_EVENT,
                                 APPLY_CREDENTIALS,
                                 nullptr,
                                 0,
                                 portMAX_DELAY));
}

void _provision_rejected_handler(MQTTContext_t *,
                                 MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGE(TAG, "Provisioning rejected");
}

void _certificate_accepted_handler(MQTTContext_t *,
                                   MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Certificate received");
  // Make a copy of the data, so we can grab the token for registration, and handle storage post-registration
  asprintf(&new_credentials_payload, "%.*s", pxPublishInfo->payloadLength, (const char *) pxPublishInfo->pPayload);

  // We receive all items required
  if (mqtt_provisioning_active()) {
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

void _certificate_rejected_handler(MQTTContext_t *,
                                   MQTTPublishInfo_t *pxPublishInfo) {
  ESP_LOGI(TAG, "Certificate rejected");
}

static void _subscribe(const char* template_name) {
  ESP_LOGI(TAG, "Subscribing to provisioning topics");

  // Always make sure we grab the right template name and re-write topics
  sprintf(topic_prov_accepted, "$aws/provisioning-templates/%s/provision/json/accepted", template_name);
  sprintf(topic_prov_rejected, "$aws/provisioning-templates/%s/provision/json/rejected", template_name);

  MQTTSubscribeInfo_t subscribeInfo[NUM_SUBSCRIPTIONS] = {
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = topic_prov_accepted,
          .topicFilterLength = (uint16_t) strlen(topic_prov_accepted)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = certificate_topic_accepted,
          .topicFilterLength = (uint16_t) strlen(certificate_topic_accepted)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = topic_prov_rejected,
          .topicFilterLength = (uint16_t) strlen(topic_prov_rejected)
      },
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = certificate_topic_rejected,
          .topicFilterLength = (uint16_t) strlen(certificate_topic_rejected)
      }
  };

  // Nothing will work if we don't have subscriptions,
  // this will block the event queue but for good reasons
  mqtt_client_subscribe(subscribeInfo, NUM_SUBSCRIPTIONS, UINT16_MAX);

  SubscriptionManager_RegisterCallback(topic_prov_accepted, strlen(topic_prov_accepted), _provision_accepted_handler);
  SubscriptionManager_RegisterCallback(certificate_topic_accepted, strlen(certificate_topic_accepted),
                                       _certificate_accepted_handler);
  SubscriptionManager_RegisterCallback(topic_prov_rejected, strlen(topic_prov_rejected), _provision_rejected_handler);
  SubscriptionManager_RegisterCallback(certificate_topic_rejected, strlen(certificate_topic_rejected),
                                       _certificate_rejected_handler);
}

static void _handle_apply_credentials() {
  assert(new_credentials_payload != nullptr);

  size_t len = strlen(new_credentials_payload);
  JSONStatus_t result = JSON_Validate(new_credentials_payload, len);
  if (result == JSONSuccess) {
    ESP_LOGI(TAG, "Processing payload to save credentials");

    uint32_t device_cert_len = 0U;
    char *device_cert;
    JSONStatus_t result1 = JSON_Search((char *) new_credentials_payload,
                                       len,
                                       "certificatePem",
                                       sizeof("certificatePem") - 1,
                                       &device_cert,
                                       (size_t *) &device_cert_len);

    uint32_t device_pk_len = 0U;
    char *device_pk;
    JSONStatus_t result2 = JSON_Search((char *) new_credentials_payload,
                                       len,
                                       "privateKey",
                                       sizeof("privateKey") - 1,
                                       &device_pk,
                                       (size_t *) &device_pk_len);
    // Cool commit new cert/pk
    if (result1 == JSONSuccess && result2 == JSONSuccess) {
      identity_save_device_auth(device_cert, device_cert_len, device_pk, device_pk_len);
    }

    // And we can now free previously allocated credentials
    free(new_credentials_payload);
    new_credentials_payload = nullptr;

    // Apply
    mqtt_client_disconnect();
    identity_reload();
    mqtt_client_init(s_networkEventGroup);

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
               (uint16_t) outValueLength, outValue, identity_thing_id());

      char *topic;
      asprintf(&topic, "$aws/provisioning-templates/%s/provision/json", template_name);
      MQTTPublishInfo_t publishInfo = {
          .qos = MQTTQoS_t::MQTTQoS1,
          .retain = false,
          .dup = false,
          .pTopicName = topic,
          .topicNameLength = (uint16_t) strlen(topic),
          .pPayload = register_thing_payload,
          .payloadLength = strlen(register_thing_payload),
      };

      mqtt_client_publish(&publishInfo, UINT16_MAX);

      free(topic);
      free(register_thing_payload);
    }
  } else {
    ESP_LOGE(TAG, "The JSON document is invalid!");
    return;
  }
}

static void _mqtt_client_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_CONNECTED_EVENT) {
    if (mqtt_provisioning_active()) {
      // Need to kick off request for new certificate by sending blank payload
      _subscribe(identity_get()->prov_template);

      const char *payload = "{}";
      const char* topic = "$aws/certificates/create/json";
      MQTTPublishInfo_t publishInfo = {
          .qos = MQTTQoS_t::MQTTQoS1,
          .retain = false,
          .dup = false,
          .pTopicName = topic,
          .topicNameLength = (uint16_t) strlen(topic),
          .pPayload = payload,
          .payloadLength = strlen(payload),
      };
      mqtt_client_publish(&publishInfo, UINT16_MAX);
    } else {
      // Normal flow then, might need to re-subscribe to expected topics
      _subscribe(identity_get()->rotate_template);
    }
  } else {

  }
}

static void provisioning_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == PROVISION_NEW_CERTIFICATE) {
    const char *template_name = identity_get()->prov_template;
    _handle_register_new_thing(template_name);
  } else if (event_id == ROTATE_CERTIFICATE) {
    const char *template_name = identity_get()->rotate_template;
    _handle_register_new_thing(template_name);
  } else if (event_id == APPLY_CREDENTIALS) {
    _handle_apply_credentials();
  }
}

void mqtt_provision_init(EventGroupHandle_t networkEventGroup) {
  s_networkEventGroup = networkEventGroup;

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(
      CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_mqtt_client_event_handler, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(
      MQTT_PROVISIONING_EVENT, ESP_EVENT_ANY_ID, &provisioning_event_handler, nullptr));

}