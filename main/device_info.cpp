#include <nvs.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include "device_info.h"
#include "shadow/shadow_handler.h"
#include "common/identity.h"

#define TAG "device_info"
#define NVS_STATS_NAMESPACE "stats"

static device_shadow_handle_t shadow_handle {};
static  bool _update_required = false;

void device_info_send(char* buffer, size_t max_len) {
  static const char* metrics_format =
    R"({
      "state": {
        "reported": {
          "thing_type:": "%s",
          "hw_version": %)" PRIu8 R"(.%)" PRIu8 R"(,
          "fw_version": "%s",
          "build_date": "%s",
          "build_type": "%s"
          }
        }
      })";
  auto identity = identity_get();
  size_t len = snprintf(buffer, max_len, metrics_format,
          CMAKE_THING_TYPE,
          identity->hardware_major, identity->hardware_minor,
          esp_app_get_description()->version,
          __DATE__, CMAKE_BUILD_TYPE);

  printf("%s\n", buffer);
  shadow_handler_update(shadow_handle, buffer, len);
  _update_required = false;
}

static void _deleted_handler(MQTTContext_t *, MQTTPublishInfo_t *pxPublishInfo) {
  // re-create the shadow then
  _update_required = true;
}

bool device_info_update_required() {
  return _update_required;
}

void device_info_init() {
  // This will push our app info as a static shadow
  _update_required = true;
  device_shadow_cfg_t shadow_cfg = {.name = "device-info", .get = null_shadow_handler, .deleted = _deleted_handler};
  shadow_handler_init(shadow_cfg, &shadow_handle);
}