#include "mqtt_ota.h"
#include "ota_os_freertos.h"
#include "ota_pal.h"
#include "ota_appversion32.h"
#include "mqtt/mqtt.h"
#include "events_common.h"
#include "core_mqtt.h"
#include "core_mqtt_agent.h"

#include <ota.h>
#include <esp_event.h>

extern "C" {
#include <osi/semaphore.h>
#include <cerrno>
#include "mqtt/pub_sub_manager.h"

}

#define TAG "ota"

/**
 * @brief Static handle used for MQTT agent context.
 */
extern MQTTAgentContext_t xGlobalMqttAgentContext;


extern const char pcAwsCodeSigningCertPem[] asm("_binary_aws_codesign_crt_start");

/**
 * @brief Struct for firmware version.
 */
const AppVersion32_t appFirmwareVersion = {
    .u = {
        .x = {.build = 1, .minor = 1, .major = 1}
    }
};

/**
 * @brief Enum for type of OTA job messages received.
 */
typedef enum jobMessageType {
  jobMessageTypeNextGetAccepted = 0,
  jobMessageTypeNextNotify,
  jobMessageTypeMax
} jobMessageType_t;


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


OtaEventData_t *otaEventBufferGet(void) {
  uint32_t ulIndex = 0;
  OtaEventData_t *pFreeBuffer = nullptr;

  if (osi_sem_take(&bufferSemaphore, 0) == 0) {
    for (ulIndex = 0; ulIndex < otaconfigMAX_NUM_OTA_DATA_BUFFERS; ulIndex++) {
      if (eventBuffer[ulIndex].bufferUsed == false) {
        eventBuffer[ulIndex].bufferUsed = true;
        pFreeBuffer = &eventBuffer[ulIndex];
        break;
      }
    }

    (void) osi_sem_give(&bufferSemaphore);
  } else {
    LogError(("Failed to get buffer semaphore: "
              ",errno=%s",
        strerror(errno)));
  }

  return pFreeBuffer;
}


/*-----------------------------------------------------------*/

static void mqttJobCallback(void *pContext,
                            MQTTPublishInfo_t *pPublishInfo) {
  OtaEventData_t *pData;
  OtaEventMsg_t eventMsg = {};
  jobMessageType_t jobMessageType;

  assert(pPublishInfo != NULL);
  (void) pContext;

  jobMessageType = getJobMessageType(pPublishInfo->pTopicName, pPublishInfo->topicNameLength);

  switch (jobMessageType) {
    case jobMessageTypeNextGetAccepted:
    case jobMessageTypeNextNotify:

      pData = otaEventBufferGet();

      if (pData != NULL) {
        memcpy(pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength);
        pData->dataLength = pPublishInfo->payloadLength;
        eventMsg.eventId = OtaAgentEventReceivedJobDocument;
        eventMsg.pEventData = pData;

        /* Send job document received event. */
        OTA_SignalEvent(&eventMsg);
      } else {
        LogError(("No OTA data buffers available."));
      }

      break;

    default:
// %zu
      LogInfo(("Received job message %s size %zu.\n\n",
          pPublishInfo->pTopicName,
          pPublishInfo->payloadLength));
  }
}

/*-----------------------------------------------------------*/

static void mqttDataCallback(void *pContext,
                             MQTTPublishInfo_t *pPublishInfo) {
  OtaEventData_t *pData;
  OtaEventMsg_t eventMsg = {};

  assert(pPublishInfo != nullptr);
  (void) pContext;

  LogInfo(("Received data message callback, size %zu.\n\n", pPublishInfo->payloadLength));

  pData = otaEventBufferGet();

  if (pData != NULL) {
    memcpy(pData->data, pPublishInfo->pPayload, pPublishInfo->payloadLength);
    pData->dataLength = pPublishInfo->payloadLength;
    eventMsg.eventId = OtaAgentEventReceivedFileBlock;
    eventMsg.pEventData = pData;

    /* Send job document received event. */
    OTA_SignalEvent(&eventMsg);
  } else {
    LogError(("No OTA data buffers available."));
  }
}

static OtaMqttStatus_t mqttSubscribe(const char *pTopicFilter,
                                     uint16_t topicFilterLength,
                                     uint8_t qos) {
  ESP_LOGI(TAG, "Request to subscribe to %.*s", topicFilterLength, pTopicFilter);

  OtaMqttStatus_t otaRet = OtaMqttSuccess;

  static const char *const pWildCardTopicFilters[] = {
      OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/#",
      OTA_TOPIC_PREFIX OTA_TOPIC_STREAM "/#"
  };

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
    if( isMatch ) {
      if (index == 0) {
        // Jobs
        mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, pTopicFilter, topicFilterLength, mqttJobCallback);
      } else {
        // Data
        mqttManagerSubscribeToTopic(MQTTQoS_t::MQTTQoS1, pTopicFilter, topicFilterLength, mqttDataCallback);
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
  auto ret = mqttPublishMessage(pacTopic, topicLen, pMsg, msgSize, (MQTTQoS_t)qos);
  if (ret != MQTTSuccess) {
    otaRet = OtaMqttPublishFailed;
  }

  return otaRet;
}

static OtaMqttStatus_t mqttUnsubscribe(const char *pTopicFilter,
                                       uint16_t topicFilterLength,
                                       uint8_t qos) {
  OtaMqttStatus_t otaRet = OtaMqttSuccess;

  return otaRet;
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
  if (osi_sem_take(&bufferSemaphore, 0) == 0) {
    pxBuffer->bufferUsed = false;
    (void) osi_sem_give(&bufferSemaphore);
  } else {
    LogError(("Failed to get buffer semaphore: "
              ",errno=%s",
        strerror(errno)));
  }
}


static void otaAppCallback(OtaJobEvent_t event,
                           void *pData) {
  OtaErr_t err = OtaErrUninitialized;
  int ret;

  switch (event) {
    case OtaJobEventActivate:
      LogInfo(("Received OtaJobEventActivate callback from OTA Agent."));

      /* Activate the new firmware image. */
      OTA_ActivateNewImage();

      /* Shutdown OTA Agent, if it is required that the unsubscribe operations are not
       * performed while shutting down please set the second parameter to 0 instead of 1. */
      OTA_Shutdown(0, 1);

      /* Requires manual activation of new image.*/
      LogError(("New image activation failed."));

      break;

    case OtaJobEventFail:
      LogInfo(("Received OtaJobEventFail callback from OTA Agent."));

      /* Nothing special to do. The OTA agent handles it. */
      break;

    case OtaJobEventStartTest:

      /* This demo just accepts the image since it was a good OTA update and esp32-networking
       * and services are all working (or we would not have made it this far). If this
       * were some custom device that wants to test other things before validating new
       * image, this would be the place to kick off those tests before calling
       * OTA_SetImageState() with the final result of either accepted or rejected. */

      LogInfo(("Received OtaJobEventStartTest callback from OTA Agent."));
      err = OTA_SetImageState(OtaImageStateAccepted);

      if (err == OtaErrNone) {
        /* Erasing passive partition */
        ret = otaPal_EraseLastBootPartition();
        if (ret != ESP_OK) {
          ESP_LOGE("otaAppCallback", "Failed to erase last boot partition! (%d)", ret);
        }
      } else {
        LogError((" Failed to set image state as accepted."));
      }

      break;

    case OtaJobEventProcessed:
      LogDebug(("Received OtaJobEventProcessed callback from OTA Agent."));

      if (pData != NULL) {
        otaEventBufferFree((OtaEventData_t *) pData);
      }

      break;

    case OtaJobEventSelfTestFailed:
      LogDebug(("Received OtaJobEventSelfTestFailed callback from OTA Agent."));

      /* Requires manual activation of previous image as self-test for
       * new image downloaded failed.*/
      LogError(("Self-test failed, shutting down OTA Agent."));

      /* Shutdown OTA Agent, if it is required that the unsubscribe operations are not
       * performed while shutting down please set the second parameter to 0 instead of 1. */
      OTA_Shutdown(0, 1);

      break;

    default:
      LogDebug(("Received invalid callback event from OTA Agent."));
  }
}

static void _event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_id == CORE_MQTT_AGENT_CONNECTED_EVENT) {
    if (!mqtt_is_provisioning()) {
      auto state = OTA_GetState();

      /* Register Jobs callback to subscription manager. */
      const char* jobsTopic = OTA_TOPIC_PREFIX OTA_TOPIC_JOBS "/#";
      addSubscription(( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                      jobsTopic,
                      strlen(jobsTopic),
                      mqttJobCallback, nullptr);

      /* Register stream callback to subscription manager. */
      const char* streamTopic = OTA_TOPIC_PREFIX OTA_TOPIC_STREAM "/#";
      addSubscription(( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                      streamTopic,
                      strlen(streamTopic),
                      mqttDataCallback, nullptr);

      /* Check if OTA process was suspended and resume if required. */
      if (state == OtaAgentStateSuspended) {
        /* Resume OTA operations. */
        ESP_LOGI(TAG, "Resuming");
        OTA_Resume();
      } else {
        /* Send start event to OTA Agent.*/
        ESP_LOGI(TAG, "Starting");
        static OtaEventMsg_t eventMsg = {.pEventData = nullptr, .eventId = OtaAgentEventStart};
        OTA_SignalEvent(&eventMsg);
      }

    }
  }
}

static void otaThread(void *pParam) {
  /* Calling OTA agent task. */
  OTA_EventProcessingTask(pParam);
  LogInfo(("OTA Agent stopped."));
  vTaskDelete(nullptr);
}

int mqtt_ota_init() {
  int returnStatus = EXIT_SUCCESS;
  OtaErr_t otaRet = OtaErrNone;

  /* Initialize semaphore for buffer operations. */
  if( osi_sem_new( &bufferSemaphore, 0x7FFFU, 1 ) != 0 )
  {
    LogError( ( "Failed to initialize buffer semaphore"
                ",errno=%s",
        strerror( errno ) ) );

    returnStatus = EXIT_FAILURE;
  }

  /* OTA interface context required for library interface functions.*/
  setOtaInterfaces(&otaInterfaces);

  ESP_ERROR_CHECK(esp_event_handler_register(CORE_MQTT_AGENT_EVENT, ESP_EVENT_ANY_ID, &_event_handler, nullptr));

  if (!otaPal_SetCodeSigningCertificate(pcAwsCodeSigningCertPem)) {
    LogError(("Failed to allocate memory for Code Signing Certificate"));
    returnStatus = EXIT_FAILURE;
  }

  LogInfo(("OTA init Application version %u.%u.%u",
      appFirmwareVersion.u.x.major,
      appFirmwareVersion.u.x.minor,
      appFirmwareVersion.u.x.build));

  if (returnStatus == EXIT_SUCCESS) {
    if ((otaRet = OTA_Init(&otaBuffer,
                           &otaInterfaces,
                           reinterpret_cast<const uint8_t *>(mqtt_thing_id()),
                           otaAppCallback)) != OtaErrNone) {
      LogError(("Failed to initialize OTA Agent, exiting = %u.",
          otaRet));

      returnStatus = EXIT_FAILURE;
    }
  }

  /****************************** Create OTA Task. ******************************/

  if (returnStatus == EXIT_SUCCESS) {
    auto res = xTaskCreate(otaThread, "ota thread", 2096 + configMINIMAL_STACK_SIZE, nullptr, 5, nullptr);
    if (res != pdPASS) {
      ESP_LOGE(TAG, "Failed to create OTA thread");
      returnStatus = EXIT_FAILURE;
    }
  }

  //asprintf(dest, "$aws/things/%s/shadow/%s", mqtt_thing_id(), command);


  return returnStatus;
}
