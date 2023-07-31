#include <nvs.h>
#include <esp_log.h>
#include "utils.h"

#define TAG "utils"

esp_err_t utils_load_from_nvs(const char* ns, const char* key, void* ptr, size_t size_in) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(ns, NVS_READWRITE, &nvs_handle));
  size_t size;
  esp_err_t err = nvs_get_blob(nvs_handle, key, nullptr, &size);
  if ( err == ESP_OK) {
    if (size != size_in) {
      ESP_LOGE(TAG, "Error loading key %s from ns %s, config from NVS has wrong size: %d va %d", key, ns, size, size_in);
    } else {
      err = nvs_get_blob(nvs_handle, "cfg", ptr, &size);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error loading key %s config from NVS %s, Error %s", key, ns, esp_err_to_name(err));
      } else {
        ESP_LOGI(TAG, "Loaded key %s from NVS ns %s", key, ns);
      }
    }
  } else {
    ESP_LOGE(TAG, "Error loading key %s config from NVS ns %s, Error: %s", key, ns, esp_err_to_name(err));
  }

  nvs_close(nvs_handle);

  return err;
}

esp_err_t utils_save_to_nvs(const char* ns, const char* key, void* ptr, size_t size) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(ns, NVS_READWRITE, &nvs_handle));
  esp_err_t err = nvs_set_blob(nvs_handle, key, ptr, size);
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  nvs_close(nvs_handle);
  return err;
}