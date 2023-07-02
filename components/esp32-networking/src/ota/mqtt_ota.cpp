#include "mqtt_ota.h"
#include "ota_os_freertos.h"
#include "ota_pal.h"
#include "ota_appversion32.h"
#include "common/events_common.h"
#include "core_mqtt.h"
#include "common/identity.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_subscription_manager.h"
#include "fleet_provisioning/mqtt_provision.h"

#include <ota.h>
#include <esp_event.h>
#include <esp_app_desc.h>
#include <esp_check.h>

extern "C" {
#include <osi/semaphore.h>
#include <cerrno>
}

#ifndef CMAKE_THING_TYPE
#error "CMAKE_THING_TYPE is undefined, please set string for your project"
#endif

#ifndef CMAKE_FIRMWARE_VERSION_BUILD
#error "CMAKE_FIRMWARE_VERSION_BUILD is undefined, please set for your FIRMWARE"
#endif
#ifndef CMAKE_FIRMWARE_VERSION_MINOR
#error "CMAKE_FIRMWARE_VERSION_MINOR is undefined, please set for your FIRMWARE"
#endif
#ifndef CMAKE_FIRMWARE_VERSION_MAJOR
#error "CMAKE_FIRMWARE_VERSION_MAJOR is undefined, please set for your FIRMWARE"
#endif

#ifndef CMAKE_HARDWARE_REVISION_MAJOR
#error "CMAKE_HARDWARE_REVISION_MAJOR is undefined, please set for your project"
#endif

#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE must be enabled"
#endif

extern const char pcAwsCodeSigningCertPem[] asm("_binary_aws_codesign_crt_start");

/**
 * @brief Struct for firmware version.
 */
const AppVersion32_t appFirmwareVersion = {.u {
    .x {
        .build = CMAKE_FIRMWARE_VERSION_BUILD,
        .minor = CMAKE_FIRMWARE_VERSION_MINOR,
        .major = CMAKE_FIRMWARE_VERSION_MAJOR,
    }
}};

/**
 * @brief Enum for type of OTA job messages received.
 */
typedef enum jobMessageType {
  jobMessageTypeNextGetAccepted = 0,
  jobMessageTypeNextNotify,
  jobMessageTypeMax
} jobMessageType_t;


#define TAG "ota"

/**
 * @brief The maximum size of the file paths used in the demo.
 */
#define OTA_MAX_FILE_PATH_SIZE                   ( 260U )

/**
 * @brief The maximum size of the stream name required for downloading update file
 * from streaming service.
 */
#define OTA_MAX_STREAM_NAME_SIZE                 ( 128U )


/**
 * @brief Update File path buffer.
 */
uint8_t updateFilePath[OTA_MAX_FILE_PATH_SIZE];

/**
 * @brief Certificate File path buffer.
 */
uint8_t certFilePath[OTA_MAX_FILE_PATH_SIZE];

/**
 * @brief Stream name buffer.
 */
uint8_t streamName[OTA_MAX_STREAM_NAME_SIZE];

/**
 * @brief Decode memory.
 */
uint8_t decodeMem[otaconfigFILE_BLOCK_SIZE];

/**
 * @brief Bitmap memory.
 */
uint8_t bitmap[OTA_MAX_BLOCK_BITMAP_SIZE];

/**
 * @brief The common prefix for all OTA topics.
 */
#define OTA_TOPIC_PREFIX    "$aws/things/+/"

/**
 * @brief The string used for jobs topics.
 */
#define OTA_TOPIC_JOBS      "jobs"

/**
 * @brief The string used for streaming service topics.
 */
#define OTA_TOPIC_STREAM    "streams"


/**
 * @brief Event buffer.
 */
static OtaEventData_t eventBuffer[otaconfigMAX_NUM_OTA_DATA_BUFFERS];


/**
 * @brief Semaphore for synchronizing buffer operations.
 */
// static osi_sem_t bufferSemaphore;
static osi_sem_t bufferSemaphore;

/**
 * @brief The buffer passed to the OTA Agent from application while initializing.
 */
static OtaAppBuffer_t otaBuffer = {
    .pUpdateFilePath    = updateFilePath,
    .updateFilePathsize = OTA_MAX_FILE_PATH_SIZE,
    .pCertFilePath      = certFilePath,
    .certFilePathSize   = OTA_MAX_FILE_PATH_SIZE,
    .pStreamName        = streamName,
    .streamNameSize     = OTA_MAX_STREAM_NAME_SIZE,
    .pDecodeMemory      = decodeMem,
    .decodeMemorySize   = otaconfigFILE_BLOCK_SIZE,
    .pFileBitmap        = bitmap,
    .fileBitmapSize     = OTA_MAX_BLOCK_BITMAP_SIZE,
    .pUrl = nullptr,
    .urlSize = 0,
    .pAuthScheme = nullptr,
    .authSchemeSize = 0
};

static OtaInterfaces_t otaInterfaces;
static EventGroupHandle_t s_networkEventGroup;

jobMessageType_t getJobMessageType(const char *pTopicName,
                                   uint16_t topicNameLength) {
  uint16_t index = 0U;
  MQTTStatus_t mqttStatus = MQTTSuccess;
  bool isMatch = false;
  jobMessageType_t jobMessageIndex = jobMessageTypeMax;

  /* For suppressing compiler-warning: unused variable. */
  (void) mqttStatus;

  /* Lookup table for OTA job message string. */
  static const char *const pJobTopicFilters[jobMessageTypeMax] =
      {
          OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/$next/get/accepted",
          OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/notify-next",
      };

  /* Match the input topic filter against the wild-card pattern of topics filters
  * relevant for the OTA Update service to determine the type of topic filter. */
  for (; index < jobMessageTypeMax; index++) {
    mqttStatus = MQTT_MatchTopic(pTopicName,
                                 topicNameLength,
                                 pJobTopicFilters[index],
                                 strlen(pJobTopicFilters[index]),
                                 &isMatch);
    assert(mqttStatus == MQTTSuccess);

    if (isMatch) {
      jobMessageIndex = static_cast<jobMessageType_t>(index);
      break;
    }
  }

  return jobMessageIndex;
}

/*-----------------------------------------------------------*/

OtaEventData_t *otaEventBufferGet() {
  uint32_t ulIndex = 0;
  OtaEventData_t *pFreeBuffer = nullptr;

  do {
    if (osi_sem_take(&bufferSemaphore, OSI_SEM_MAX_TIMEOUT) == 0) {
      for (ulIndex = 0; ulIndex < otaconfigMAX_NUM_OTA_DATA_BUFFERS; ulIndex++) {
        if (pFreeBuffer == nullptr && !eventBuffer[ulIndex].bufferUsed) {
          eventBuffer[ulIndex].bufferUsed = true;
          pFreeBuffer = &eventBuffer[ulIndex];
        }
      }

      osi_sem_give(&bufferSemaphore);
    } else {
      LogError(("Failed to get buffer semaphore: ,errno=%s", strerror(errno)));
    }

    // We only have a limited amount of buffers (4 minimum)
    // This busy wait is deliberate, so we get the ota task a chance to process the last request
    if (pFreeBuffer == nullptr) {
      ESP_LOGW(TAG, "No buffers left, waiting...");
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  } while (pFreeBuffer == nullptr);

  return pFreeBuffer;
}

/*-----------------------------------------------------------*/

static void mqttJobCallback(MQTTContext *,
                            MQTTPublishInfo_t *pPublishInfo) {
  OtaEventData_t *pData;
  OtaEventMsg_t eventMsg = {};
  jobMessageType_t jobMessageType;

  assert(pPublishInfo != NULL);

  jobMessageType = getJobMessageType(pPublishInfo->pTopicName, pPublishInfo->topicNameLength);

  switch (jobMessageType) {
    case jobMessageTypeNextGetAccepted:
    case jobMessageTypeNextNotify:

      pData = otaEventBufferGet();
      memcpy(pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength);
      pData->dataLength = pPublishInfo->payloadLength;
      eventMsg.eventId = OtaAgentEventReceivedJobDocument;
      eventMsg.pEventData = pData;
      OTA_SignalEvent(&eventMsg);
      break;

    default:
      ESP_LOGI(TAG, "Received job message %s size %zu.\n\n",
          pPublishInfo->pTopicName,
          pPublishInfo->payloadLength);
  }
}

/*-----------------------------------------------------------*/

static void mqttDataCallback(MQTTContext *,
                             MQTTPublishInfo_t *pPublishInfo) {
  OtaEventData_t *pData;
  OtaEventMsg_t eventMsg = {};

  assert(pPublishInfo != nullptr);
  pData = otaEventBufferGet();
  memcpy(pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength);
  pData->dataLength = pPublishInfo->payloadLength;
  eventMsg.eventId = OtaAgentEventReceivedFileBlock;
  eventMsg.pEventData = pData;
  OTA_SignalEvent(&eventMsg);
}

static OtaMqttStatus_t mqttSubscribe(const char *pTopicFilter,
                                     uint16_t topicFilterLength,
                                     uint8_t qos) {
  OtaMqttStatus_t otaRet = OtaMqttSuccess;

  static const char *const pWildCardTopicFilters[] = {
      OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/#",
      OTA_TOPIC_PREFIX OTA_TOPIC_STREAM "/#"
  };

  static const int NUM_SUBSCRIPTIONS = 1;
  MQTTSubscribeInfo_t subscribeInfo[NUM_SUBSCRIPTIONS] = {
      {
          .qos = MQTTQoS::MQTTQoS1,
          .pTopicFilter = pTopicFilter,
          .topicFilterLength = (uint16_t) topicFilterLength
      }
  };

  // Nothing will work if we don't have subscriptions,
  // this will block the event queue but for good reasons
  mqtt_client_subscribe(subscribeInfo, NUM_SUBSCRIPTIONS, UINT16_MAX);

  /* Match the input topic filter against the wild-card pattern of topics filters
   * relevant for the OTA Update service to determine the type of topic filter. */
  for (int index = 0; index < 2; index++) {
    bool isMatch;
    auto mqttStatus = MQTT_MatchTopic(pTopicFilter,
                                      topicFilterLength,
                                      pWildCardTopicFilters[index],
                                      strlen(pWildCardTopicFilters[index]),
                                      &isMatch);
    assert(mqttStatus == MQTTSuccess);
    if (isMatch) {
      if (index == 0) {
        // Jobs
        SubscriptionManager_RegisterCallback(pWildCardTopicFilters[index], strlen(pWildCardTopicFilters[index]),
                                             mqttJobCallback);
      } else {
        // Data
        SubscriptionManager_RegisterCallback(pWildCardTopicFilters[index], strlen(pWildCardTopicFilters[index]),
                                             mqttDataCallback);
      }
    }
  }


  return otaRet;
}

static OtaMqttStatus_t mqttPublish(const char *const pacTopic,
                                   uint16_t topicLen,
                                   const char *pMsg,
                                   uint32_t msgSize,
                                   uint8_t qos) {
  OtaMqttStatus_t otaRet = OtaMqttSuccess;

  MQTTPublishInfo_t publishInfo = {
      .qos = (MQTTQoS_t) qos,
      .retain = false,
      .dup = false,
      .pTopicName = pacTopic,
      .topicNameLength = topicLen,
      .pPayload = pMsg,
      .payloadLength = msgSize,
  };

  // Note how ack wait is zero, we want non-blocking.
  int ret = mqtt_client_publish(&publishInfo, 0);
  if (ret != EXIT_SUCCESS) {
    otaRet = OtaMqttPublishFailed;
  }

  return otaRet;
}

static OtaMqttStatus_t mqttUnsubscribe(const char *pTopicFilter,
                                       uint16_t topicFilterLength,
                                       uint8_t qos) {
  static const int NUM_SUBSCRIPTIONS = 1;
  MQTTSubscribeInfo_t subscribeInfo[NUM_SUBSCRIPTIONS] = {
      {
          .qos = (MQTTQoS_t) qos,
          .pTopicFilter = pTopicFilter,
          .topicFilterLength = (uint16_t) topicFilterLength
      }
  };

  auto res = mqtt_client_unsubscribe(subscribeInfo, NUM_SUBSCRIPTIONS, CONFIG_MQTT_ACK_TIMEOUT_MS);
  return res == EXIT_SUCCESS ? OtaMqttSuccess : OtaMqttUnsubscribeFailed;
}

static void setOtaInterfaces(OtaInterfaces_t *pOtaInterfaces) {
  /* Initialize OTA library OS Interface. */
  pOtaInterfaces->os.event.init = OtaInitEvent_FreeRTOS;
  pOtaInterfaces->os.event.send = OtaSendEvent_FreeRTOS;
  pOtaInterfaces->os.event.recv = OtaReceiveEvent_FreeRTOS;
  pOtaInterfaces->os.event.deinit = OtaDeinitEvent_FreeRTOS;
  pOtaInterfaces->os.timer.start = OtaStartTimer_FreeRTOS;
  pOtaInterfaces->os.timer.stop = OtaStopTimer_FreeRTOS;
  pOtaInterfaces->os.timer.deleteTimer = OtaDeleteTimer_FreeRTOS;
  pOtaInterfaces->os.mem.malloc = Malloc_FreeRTOS;
  pOtaInterfaces->os.mem.free = Free_FreeRTOS;

  /* Initialize the OTA library MQTT Interface.*/
  pOtaInterfaces->mqtt.subscribe = mqttSubscribe;
  pOtaInterfaces->mqtt.publish = mqttPublish;
  pOtaInterfaces->mqtt.unsubscribe = mqttUnsubscribe;

  /* Initialize the OTA library PAL Interface.*/
  pOtaInterfaces->pal.getPlatformImageState = otaPal_GetPlatformImageState;
  pOtaInterfaces->pal.setPlatformImageState = otaPal_SetPlatformImageState;
  pOtaInterfaces->pal.writeBlock = otaPal_WriteBlock;
  pOtaInterfaces->pal.activate = otaPal_ActivateNewImage;
  pOtaInterfaces->pal.closeFile = otaPal_CloseFile;
  pOtaInterfaces->pal.reset = otaPal_ResetDevice;
  pOtaInterfaces->pal.abort = otaPal_Abort;
  pOtaInterfaces->pal.createFile = otaPal_CreateFileForRx;
}


void otaEventBufferFree(OtaEventData_t *const pxBuffer) {
  if (osi_sem_take(&bufferSemaphore, OSI_SEM_MAX_TIMEOUT) == 0) {
    pxBuffer->bufferUsed = false;
    (void) osi_sem_give(&bufferSemaphore);
  } else {
    LogError(("Failed to get buffer semaphore: "
              ",errno=%s",
        strerror(errno)));
  }
}

static bool _validate_new_image() {
  ESP_LOGI(TAG, "Validating new image");
  bool is_ok = true;

  // Check thing type is valid and also hardware revision is compatible
  if (strcmp(CMAKE_THING_TYPE, identity_get()->thing_type) != 0) {
    ESP_LOGE(TAG, "Thing type is incompatible %s vs %s",
             CMAKE_THING_TYPE, identity_get()->thing_type);
    is_ok = false;
  }

  if (CMAKE_HARDWARE_REVISION_MAJOR != identity_get()->hardware_major) {
    ESP_LOGE(TAG, "Hardware major version is incompatible %d vs %d",
             CMAKE_HARDWARE_REVISION_MAJOR, identity_get()->hardware_major);
    is_ok = false;
  }

  return is_ok;
}


static void otaAppCallback(OtaJobEvent_t event, void *pData) {
  switch (event) {
    case OtaJobEventActivate: {
      ESP_LOGI(TAG, "Received OtaJobEventActivate callback from OTA Agent.");

      /* Activate the new firmware image - this call does not return when successful (reset device) */
      // We've just finished downloading a new firmware, this sets it and devices needs restarting
      OTA_ActivateNewImage();
      LogError(("New image activation failed."));

      break;
    }
    case OtaJobEventFail: {
      ESP_LOGI(TAG, "Received OtaJobEventFail callback from OTA Agent.");

      /* Nothing special to do. The OTA agent handles it. */
      break;
    }
    case OtaJobEventStartTest: {
      // Set test sequence

      if (_validate_new_image()) {
        ESP_LOGI(TAG, "\n\n!! New image is valid, accepting !!\n\n");
        OTA_SetImageState(OtaImageStateAccepted);

        // And we are done with OTA, application caries on as usual
        xEventGroupClearBits(s_networkEventGroup, CORE_MQTT_OTA_IN_PROGRESS_BIT);
        esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_OTA_STOPPED_EVENT, NULL, 0, portMAX_DELAY);
      } else {
        LogError(("\n\n!! New image is invalid, rejecting !!\n\n"));
        OTA_SetImageState(OtaImageStateRejected);

        // After testing phase completes, we need a manual restart
        esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_TEST_FAILED_RESTART, NULL, 0, portMAX_DELAY);
      }

      break;
    }
    case OtaJobEventProcessed: {
      ESP_LOGD(TAG, "Received OtaJobEventProcessed callback from OTA Agent.");

      if (pData != NULL) {
        otaEventBufferFree((OtaEventData_t *) pData);
      }

      break;
    }
    case OtaJobEventSelfTestFailed: {
      LogError(("Self-test failed, shutting down OTA Agent."));
      // Restart is handled by OTA libraries.
      break;
    }
    case OtaJobEventReceivedJob: {
      ESP_LOGI(TAG, "Receive OTA start event");
      xEventGroupSetBits(s_networkEventGroup, CORE_MQTT_OTA_IN_PROGRESS_BIT);
      esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_OTA_STARTED_EVENT, NULL, 0, portMAX_DELAY);
      break;
    }
    case OtaJobEventUpdateComplete: {
      ESP_LOGI(TAG, "Receive OTA completed event");
      xEventGroupClearBits(s_networkEventGroup, CORE_MQTT_OTA_IN_PROGRESS_BIT);
      esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_OTA_STOPPED_EVENT, NULL, 0, portMAX_DELAY);
      break;
    }
    default: {
      ESP_LOGD(TAG, "Received invalid callback event from OTA Agent.");
    }
  }
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  static OtaEventMsg_t eventMsg = {};
  if (event_id == CORE_MQTT_CONNECTED_EVENT) {
    if (!mqtt_provisioning_active()) {
      auto state = OTA_GetState();
      /* Check if OTA process was suspended and resume if required. */
      if (state == OtaAgentStateSuspended) {
        /* Resume OTA operations. */
        ESP_LOGI(TAG, ">> Resuming");
        xEventGroupSetBits(s_networkEventGroup, CORE_MQTT_OTA_IN_PROGRESS_BIT);
        OTA_Resume();
      } else if (state <= OtaAgentStateReady || state == OtaAgentStateStopped){
        /* Send start event to OTA Agent.*/
        ESP_LOGI(TAG, ">> Starting");
        eventMsg = {.pEventData = nullptr, .eventId = OtaAgentEventStart};
        OTA_SignalEvent(&eventMsg);
      } else {
        // restart
        OTA_Suspend();
        OTA_Resume();
      }
    }
  } else if (event_id == CORE_MQTT_TEST_FAILED_RESTART) {
    ESP_LOGI(TAG, "Shutting OTA service");
    OTA_Shutdown(portMAX_DELAY, 1);
    ESP_LOGI(TAG, "Restarting platform");
    esp_restart();
  }
}

static void otaThread(void *pParam) {
  /* Calling OTA agent task. */
  OTA_EventProcessingTask(pParam);
  ESP_LOGI(TAG, "OTA Agent stopped.");
  vTaskDelete(nullptr);
}

esp_err_t mqtt_ota_init(EventGroupHandle_t networkEventGroup) {
  s_networkEventGroup = networkEventGroup;
  esp_err_t ret = ESP_OK;
  OtaErr_t otaRet = OtaErrNone;

  // Clear OTA flag
  xEventGroupClearBits(networkEventGroup, CORE_MQTT_OTA_IN_PROGRESS_BIT);

  ESP_LOGI(TAG, "Initialising OTA for Firmware version is %" PRIu8 ".%" PRIu8 "-%" PRIu16,
           appFirmwareVersion.u.x.major,
           appFirmwareVersion.u.x.minor,
           appFirmwareVersion.u.x.build);

  /* Initialize semaphore for buffer operations. */
  if (osi_sem_new(&bufferSemaphore, 0x7FFFU, 1) != 0) {
    LogError(("Failed to initialize buffer semaphore, errno=%s", strerror(errno)));
    ret = ESP_FAIL;
    goto error;
  }

  /* OTA interface context required for library interface functions.*/
  setOtaInterfaces(&otaInterfaces);

  ESP_GOTO_ON_ERROR(esp_event_handler_register(CORE_MQTT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr),
                    error, TAG, "Failed to register event handler for MQTT");

  if (!otaPal_SetCodeSigningCertificate(pcAwsCodeSigningCertPem)) {
    LogError(("Failed to allocate memory for Code Signing Certificate"));
    ret = ESP_FAIL;
    goto error;
  }

  if ((otaRet = OTA_Init(&otaBuffer,
                         &otaInterfaces,
                         reinterpret_cast<const uint8_t *>(identity_thing_id()),
                         otaAppCallback)) != OtaErrNone) {
    LogError(("Failed to initialize OTA Agent, exiting = %u.",
        otaRet));

    ret = ESP_FAIL;
    goto error;
  }

  if (xTaskCreate(otaThread, "ota thread", 4096, nullptr, 5, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create OTA thread");
    goto error;
  }

  return ret;

  error:
  ESP_LOGE(TAG, "Init failed, OTA not available.");
  return ret;
}
