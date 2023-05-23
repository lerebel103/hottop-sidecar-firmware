#include "freertos/FreeRTOS.h"
#include "mqtt.h"

#include <nvs_flash.h>
#include <esp_log.h>
#include <freertos/event_groups.h>
#include "sntp.h"
#include "mqtt_mutual_auth.h"

#define TAG "mqtt"
#define NVS_IDENTITY_NAMESPACE        "identity"

#define NVS_THING_TYPE_KEY            "thing_type"
#define NVS_STAGE_NAME_KEY            "stage_name"
#define NVS_ATS_ENDPOINT_KEY          "ats_ep"
#define NVS_JOBS_ENDPOINT_KEY         "jobs_ep"
#define NVS_CA_CERT_KEY               "ca_cert"
#define NVS_PROV_CERT_KEY             "prov_cert"
#define NVS_PROV_PRIVATE_KEY_KEY      "prov_key"
#define NVS_DEVICE_CERT_KEY           "device_cert"
#define NVS_DEVICE_PRIVATE_KEY_KEY    "device_key"

extern EventGroupHandle_t comms_event_group;

static identity_t s_identity;

static void _run_comms_async(void *) {
  xEventGroupWaitBits(comms_event_group, TIME_SYNC_BIT, false, true, portMAX_DELAY);
  ESP_LOGI(TAG, "Connecting to ATS endpoint: %s", s_identity.ats_ep);

  aws_iot(s_identity);

  // Clean up self.
  vTaskDelete(NULL);
}



void _load_identity() {
  nvs_handle_t nvs_handle;
  size_t len;
  ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, nullptr, &len));
  s_identity.thing_type = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, s_identity.thing_type, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_STAGE_NAME_KEY, nullptr, &len));
  s_identity.stage_name = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_STAGE_NAME_KEY, s_identity.stage_name, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, nullptr, &len));
  s_identity.ats_ep = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, s_identity.ats_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, nullptr, &len));
  s_identity.jobs_ep = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, s_identity.jobs_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, nullptr, &len));
  s_identity.ca_cert = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, s_identity.ca_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, nullptr, &len));
  s_identity.prov_cert = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, s_identity.prov_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, nullptr, &len));
  s_identity.prov_private_key = (char*)calloc(len, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, s_identity.prov_private_key, &len));

  // optional
  esp_err_t err = nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, nullptr, &len);
  if (err == ESP_OK) {
    s_identity.device_cert = (char*)calloc(len, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, s_identity.device_cert, &len));
  }

  // optional
  err = nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, nullptr, &len);
  if (err == ESP_OK) {
    s_identity.device_private_key = (char *) calloc(len, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, s_identity.device_private_key, &len));
  }

  nvs_close(nvs_handle);
}

esp_err_t mqtt_init() {
  ESP_LOGI(TAG, "Init");

  if (s_identity.ats_ep != nullptr) {
    ESP_LOGE(TAG, "Already configured");
    return ESP_FAIL;
  }

  _load_identity();

  xTaskCreate(_run_comms_async, "MQTT task", 8096, nullptr, 5, nullptr);

  return ESP_OK;
}