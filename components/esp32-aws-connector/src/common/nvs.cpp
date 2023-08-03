#include "nvs.h"
#include "events_common.h"
#include <nvs_flash.h>
#include <esp_log.h>

#define IDENTITY_NS "identity"
#define NVS_FACTORY_PART_NAME "nvs_factory"
#define MAX_VALUE_LEN (10*1024)

#define TAG "nvs"

static void _nvs_factory_restore_commit() {
  ESP_LOGW(TAG, "Restoring NVS from Factory settings.");

  if (nvs_flash_init_partition(NVS_FACTORY_PART_NAME) == ESP_OK) {

    // Open factory NVS as read-only
    nvs_handle factory_handle;
    ESP_ERROR_CHECK(nvs_open_from_partition(NVS_FACTORY_PART_NAME, IDENTITY_NS, NVS_READONLY,
                                            &factory_handle));

    // Open working NVS partition as read/write
    nvs_handle working_handle;
    ESP_ERROR_CHECK(nvs_open_from_partition(NVS_DEFAULT_PART_NAME, IDENTITY_NS, NVS_READWRITE,
                                            &working_handle));

    char* copy_buffer = (char*)calloc(MAX_VALUE_LEN, 1);

    // Good, now all we have to do is shift all keys across, and we are done
    int keys_count = 0;
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find(NVS_FACTORY_PART_NAME, IDENTITY_NS, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
      nvs_entry_info_t info;
      ESP_ERROR_CHECK(nvs_entry_info(it, &info));

      switch (info.type) {
        case NVS_TYPE_U8: { /*!< Type uint8_t */
          uint8_t val;
          nvs_get_u8(factory_handle, info.key, &val);
          nvs_set_u8(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_I8: {  /*!< Type int8_t */
          int8_t val;
          nvs_get_i8(factory_handle, info.key, &val);
          nvs_set_i8(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_U16: { /*!< Type uint16_t */
          uint16_t val;
          nvs_get_u16(factory_handle, info.key, &val);
          nvs_set_u16(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_I16: { /*!< Type int16_t */
          int16_t val;
          nvs_get_i16(factory_handle, info.key, &val);
          nvs_set_i16(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_U32: { /*!< Type uint32_t */
          uint32_t val;
          nvs_get_u32(factory_handle, info.key, &val);
          nvs_set_u32(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_I32: { /*!< Type int32_t */
          int32_t val;
          nvs_get_i32(factory_handle, info.key, &val);
          nvs_set_i32(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_U64: { /*!< Type uint64_t */
          uint64_t val;
          nvs_get_u64(factory_handle, info.key, &val);
          nvs_set_u64(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_I64: { /*!< Type int64_t */
          int64_t val;
          nvs_get_i64(factory_handle, info.key, &val);
          nvs_set_i64(working_handle, info.key, val);
          break;
        }
        case NVS_TYPE_STR: { /*!< Type string */
          size_t len = MAX_VALUE_LEN;
          nvs_get_str(factory_handle, info.key, copy_buffer, &len);
          nvs_set_str(working_handle, info.key, copy_buffer);
          break;
        }
        case NVS_TYPE_BLOB: { /*!< Type blob */
          size_t len = MAX_VALUE_LEN;
          nvs_get_blob(factory_handle, info.key, copy_buffer, &len);
          nvs_set_blob(working_handle, info.key, copy_buffer, len);
          break;
        }
        default:
          break;
      }

      // Save what's just been written
      nvs_commit(working_handle);

      res = nvs_entry_next(&it);
      ++ keys_count;
    }

    nvs_release_iterator(it);
    free(copy_buffer);

    // Close handles
    nvs_close(factory_handle);
    nvs_close(working_handle);

    ESP_LOGI(TAG, "Restored %d NVS keys from factory", keys_count);
  } else {
    // Nothing much we can do sadly... This means certain death.
    ESP_LOGE(TAG, "Unable to open factory NVS, this is very bad.");
  }
}

esp_err_t nvs_init() {
  ESP_LOGI(TAG, "Initialising NVRAM");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  int keys_count = 0;

  // Simply count the number of keys in our NVS store to decide if we are empty or not
  if (nvs_flash_init_partition(NVS_DEFAULT_PART_NAME) == ESP_OK) {
    // ***Note***: We use a single nvs partition namespace for the entire app, which is an oversight really
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find(NVS_DEFAULT_PART_NAME, IDENTITY_NS, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
      res = nvs_entry_next(&it);
      ++keys_count;
    }
    nvs_release_iterator(it);

    if (keys_count == 0) {
      _nvs_factory_restore_commit();
    }
    return ESP_OK;
  } else {
    return ESP_FAIL;
  }



}