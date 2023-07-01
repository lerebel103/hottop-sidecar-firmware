#include "freertos/FreeRTOS.h"
#include "mqtt_client.h"

#include <string>
#include <esp_log.h>
#include <freertos/event_groups.h>
#include <cstring>
#include <freertos/semphr.h>
#include <esp_mac.h>
#include <esp_event.h>

#include "events_common.h"

#include "core_json.h"
#include "core_mqtt.h"
#include "transport_interface.h"
#include "network_transport.h"
#include "backoff_algorithm.h"
#include "clock.h"
#include "mqtt_subscription_manager.h"
#include "common/identity.h"


#define TAG "mqtt"

ESP_EVENT_DEFINE_BASE(CORE_MQTT_EVENT);


/**
 * @brief Size of the network buffer for MQTT packets.
 */
#define NETWORK_BUFFER_SIZE       ( CONFIG_MQTT_NETWORK_BUFFER_SIZE )

/**
 * @brief The maximum number of retries for connecting to server.
 */
#define CONNECTION_RETRY_MAX_ATTEMPTS            ( 5U )

/**
 * @brief The maximum back-off delay (in milliseconds) for retrying connection to server.
 */
#define CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS    ( 5000U )

/**
 * @brief The base back-off delay (in milliseconds) to use for connection retry attempts.
 */
#define CONNECTION_RETRY_BACKOFF_BASE_MS         ( 500U )

/**
 * @brief Timeout for receiving CONNACK packet in milli seconds.
 */
#define CONNACK_RECV_TIMEOUT_MS                  ( 5000U )

/**
 * @brief Maximum number of outgoing publishes maintained in the application
 * until an ack is received from the broker.
 */
#define MAX_OUTGOING_PUBLISHES              ( 5U )

/**
 * @brief Invalid packet identifier for the MQTT packets. Zero is always an
 * invalid packet identifier as per MQTT 3.1.1 spec.
 */
#define MQTT_PACKET_ID_INVALID              ( ( uint16_t ) 0U )

/**
 * @brief Timeout for MQTT_ProcessLoop function in milliseconds.
 */
#define MQTT_PROCESS_LOOP_TIMEOUT_MS        ( 5000U )

/**
 * @brief The maximum time interval in seconds which is allowed to elapse
 *  between two Control Packets.
 *
 *  It is the responsibility of the Client to ensure that the interval between
 *  Control Packets being sent does not exceed the this Keep Alive value. In the
 *  absence of sending any other Control Packets, the Client MUST send a
 *  PINGREQ Packet.
 */
#define MQTT_KEEP_ALIVE_INTERVAL_SECONDS    ( 60U )

/**
 * @brief Delay between MQTT publishes in seconds.
 */
#define DELAY_BETWEEN_PUBLISHES_SECONDS     ( 1U )

/**
 * @brief Number of PUBLISH messages sent per iteration.
 */
#define MQTT_PUBLISH_COUNT_PER_LOOP         ( 5U )

/**
 * @brief Delay in seconds between two iterations of subscribePublishLoop().
 */
#define MQTT_SUBPUB_LOOP_DELAY_SECONDS      ( 5U )

/**
 * @brief Transport timeout in milliseconds for transport send and receive.
 */
#define TRANSPORT_SEND_RECV_TIMEOUT_MS      ( 1500U )

/**
 * @brief The name of the operating system that the application is running on.
 * The current value is given as an example. Please update for your specific
 * operating system.
 */
#define OS_NAME                   "FreeRTOS"

/**
 * @brief The version of the operating system that the application is running
 * on. The current value is given as an example. Please update for your specific
 * operating system version.
 */
#define OS_VERSION                tskKERNEL_VERSION_NUMBER

/**
 * @brief The name of the hardware platform the application is running on. The
 * current value is given as an example. Please update for your specific
 * hardware platform.
 */
#define HARDWARE_PLATFORM_NAME    "ESP32"

/**
 * @brief The name of the MQTT library used and its version, following an "@"
 * symbol.
 */
#define MQTT_LIB    "core-mqtt@" MQTT_LIBRARY_VERSION

/**
 * @brief The MQTT metrics string expected by AWS IoT.
 */
#define METRICS_STRING                      "?SDK=" OS_NAME "&Version=" OS_VERSION "&Platform=" HARDWARE_PLATFORM_NAME "&MQTTLib=" MQTT_LIB

/**
 * @brief The length of the MQTT metrics string expected by AWS IoT.
 */
#define METRICS_STRING_LENGTH               ( ( uint16_t ) ( sizeof( METRICS_STRING ) - 1 ) )

/**
 * @brief The length of the outgoing publish records array used by the coreMQTT
 * library to track QoS > 0 packet ACKS for outgoing publishes.
 */
#define OUTGOING_PUBLISH_RECORD_LEN    ( 10U )

/**
 * @brief The length of the incoming publish records array used by the coreMQTT
 * library to track QoS > 0 packet ACKS for incoming publishes.
 */
#define INCOMING_PUBLISH_RECORD_LEN    ( 10U )

/**
 * @brief ALPN (Application-Layer Protocol Negotiation) protocol name for AWS IoT MQTT.
 *
 * This will be used if the AWS_MQTT_PORT is configured as 443 for AWS IoT MQTT broker.
 * Please see more details about the ALPN protocol for AWS IoT MQTT endpoint
 * in the link below.
 * https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/
 */
#define AWS_IOT_MQTT_ALPN               "x-amzn-mqtt-ca"

/**
 * @brief The task stack size of the coreMQTT-Agent task.
 */
#define configMQTT_TASK_STACK_SIZE                ( CONFIG_MQTT_TASK_STACK_SIZE )

/**
 * @brief The task priority of the coreMQTT-Agent task.
 */
#define configMQTT_TASK_PRIORITY                  ( CONFIG_MQTT_TASK_PRIORITY )

#define MAX_SUB_REQUESTS 32

/*-----------------------------------------------------------*/

/**
 * @brief Structure to keep the MQTT publish packets until an ack is received
 * for QoS1 publishes.
 */
typedef struct PublishPackets {
  /**
   * @brief Packet identifier of the publish packet.
   */
  uint16_t packetId;

  /**
   * @brief Publish info of the publish packet.
   */
  MQTTPublishInfo_t pubInfo;
} PublishPackets_t;


/*-----------------------------------------------------------*/

static bool _go = false;

/**
 * @brief Packet Identifier updated when an ACK packet is received.
 *
 * It is used to match an expected ACK for a transmitted packet.
 */
static uint16_t globalAckPacketIdentifier = 0U;

/**
 * @brief The network buffer must remain valid for the lifetime of the MQTT context.
 */
static uint8_t buffer[NETWORK_BUFFER_SIZE];

/**
 * @brief Status of latest Subscribe ACK;
 * it is updated every time the callback function processes a Subscribe ACK
 * and accounts for subscription to a single topic.
 */
static MQTTSubAckStatus_t globalSubAckStatus[MAX_SUB_REQUESTS];

static size_t numSubAckStatus = 0;

/**
 * @brief Array to track the outgoing publish records for outgoing publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pOutgoingPublishRecords[OUTGOING_PUBLISH_RECORD_LEN];

/**
 * @brief Array to track the incoming publish records for incoming publishes
 * with QoS > 0.
 *
 * This is passed into #MQTT_InitStatefulQoS to allow for QoS > 0.
 *
 */
static MQTTPubAckInfo_t pIncomingPublishRecords[INCOMING_PUBLISH_RECORD_LEN];

SemaphoreHandle_t mqttMutex = xSemaphoreCreateRecursiveMutex();
SemaphoreHandle_t ackMutex = xSemaphoreCreateMutex();
TaskHandle_t mqtt_loop_task;

/*-----------------------------------------------------------*/


static MQTTContext_t xMqttContext = {};
static NetworkContext_t xNetworkContext = {};
static MQTTFixedBuffer_t networkBuffer;
static EventGroupHandle_t s_networkEventGroup;

ESP_EVENT_DEFINE_BASE(MQTT_PROVISIONING_EVENT);

static bool mqtt_is_initialised() {
  return mqtt_loop_task != nullptr;
}

static BaseType_t prvInitializeNetworkContext(void) {
  /* This is returned by this function. */
  BaseType_t xRet = pdPASS;

  /* Verify that the MQTT endpoint and thing name have been configured by the
   * user. */
  if (strlen(identity_get()->ats_ep) == 0) {
    ESP_LOGE(TAG, "Empty endpoint for MQTT broker.");
    xRet = pdFAIL;
  }

  if (strlen(identity_thing_id()) == 0) {
    ESP_LOGE(TAG, "Empty thingname for MQTT broker.");
    xRet = pdFAIL;
  }

  /* Initialize network context. */

  xNetworkContext.pcHostname = identity_get()->ats_ep;
  xNetworkContext.xPort = 443;

  /* Initialize credentials for establishing TLS session. */
  xNetworkContext.pcServerRootCA = identity_get()->ca_cert;
  xNetworkContext.pcServerRootCASize = strlen(identity_get()->ca_cert) + 1;

  char *cert = identity_get()->device_cert;
  char *private_key = identity_get()->device_private_key;
  if (cert == nullptr || private_key == nullptr) {
    ESP_LOGW(TAG, "No client credentials, loading provisioning certificate");
    cert = identity_get()->prov_cert;
    private_key = identity_get()->prov_private_key;
  }

  xNetworkContext.pcClientCert = cert;
  xNetworkContext.pcClientCertSize = strlen(cert) + 1;
  xNetworkContext.pcClientKey = private_key;
  xNetworkContext.pcClientKeySize = strlen(private_key) + 1;
  xNetworkContext.pxTls = NULL;
  xNetworkContext.xTlsContextSemaphore = xSemaphoreCreateMutex();
  xNetworkContext.disableSni = 0;

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

/*-----------------------------------------------------------*/

static int waitForPacketAck(MQTTContext_t *pMqttContext,
                            uint16_t usPacketIdentifier,
                            uint32_t ulTimeout) {
  // If no wait timeout, then client code does not want to wait for ACK
  if (ulTimeout == 0) {
    return (xEventGroupGetBits(s_networkEventGroup) & CORE_MQTT_CLIENT_CONNECTED_BIT) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  uint32_t ulCurrentTime = pMqttContext->getTime();
  uint32_t ulMqttProcessLoopTimeoutTime = ulCurrentTime + ulTimeout;

  /* wait for packet match within allowed time period */
  while ((globalAckPacketIdentifier != usPacketIdentifier) &&
         (ulCurrentTime < ulMqttProcessLoopTimeoutTime) &&
         (xEventGroupGetBits(s_networkEventGroup) & CORE_MQTT_CLIENT_CONNECTED_BIT) &&
         _go) {
    vTaskDelay(pdMS_TO_TICKS(100));
    ulCurrentTime = pMqttContext->getTime();
  }

  return (globalAckPacketIdentifier != usPacketIdentifier) ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*-----------------------------------------------------------*/

static int
subscribe_command(bool is_subscribe, const MQTTSubscribeInfo_t *topics, size_t numTopics, uint16_t ackWaitMS) {
  if (!mqtt_is_initialised()) {
    ESP_LOGE(TAG, "MQTT client is not running");
    return EXIT_FAILURE;
  }

  int returnStatus = EXIT_SUCCESS;
  MQTTStatus_t mqttStatus = MQTTStatus_t::MQTTSuccess;
  uint16_t packetId = 0;
  const char *operation_name = is_subscribe ? "subscribe" : "unsuscribe";

  for (int i = 0; i < numTopics; i++) {
    LogInfo(("%s to %.*s", operation_name, topics[i].topicFilterLength, topics[i].pTopicFilter));
  }

  do {
    xSemaphoreTake(ackMutex, portMAX_DELAY);
    {
      /* Send SUBSCRIBE packet. */
      xSemaphoreTake(mqttMutex, portMAX_DELAY);
      {
        if (_go) {
          packetId = MQTT_GetPacketId(&xMqttContext);

          /* Reset the ACK packet identifier being received, and number of expected status. */
          globalAckPacketIdentifier = 0;

          if (is_subscribe) {
            numSubAckStatus = 0;
            mqttStatus = MQTT_Subscribe(&xMqttContext,
                                        topics,
                                        numTopics,
                                        packetId);
          } else {
            numSubAckStatus = numTopics;
            mqttStatus = MQTT_Unsubscribe(&xMqttContext,
                                          topics,
                                          numTopics,
                                          packetId);
          }
        } else {
          returnStatus = EXIT_FAILURE;
        }
      }
      xSemaphoreGive(mqttMutex);

      if (mqttStatus != MQTTSuccess) {
        LogError(("Failed to send %s packet to broker with error = %s.", operation_name,
            MQTT_Status_strerror(mqttStatus)));
        returnStatus = EXIT_FAILURE;
      } else {
        returnStatus = waitForPacketAck(&xMqttContext, packetId,
                                        ackWaitMS == UINT16_MAX ? CONFIG_MQTT_ACK_TIMEOUT_MS : ackWaitMS);

        // Check each status now
        if (returnStatus == EXIT_SUCCESS && numSubAckStatus == numTopics) {
          for (int i = 0; i < numTopics; i++) {
            if (is_subscribe && globalSubAckStatus[i] == MQTTSubAckFailure) {
              returnStatus = EXIT_FAILURE;
              break;
            }
          }
        }
      }
    }
    xSemaphoreGive(ackMutex);

    // Report on console
    for (int i = 0; i < numTopics; i++) {
      if (returnStatus == EXIT_FAILURE) {
        LogError(("Failed to %s to %.*s", operation_name, topics[i].topicFilterLength, topics[i].pTopicFilter));
      } else {
        LogDebug(("%s to %.*s OK.", operation_name, topics[i].topicFilterLength, topics[i].pTopicFilter));
      }
    }

    // Don't kill CPU
    if (_go && returnStatus == EXIT_FAILURE) {
      vTaskDelay(CONNECTION_RETRY_BACKOFF_BASE_MS);
    }

  } while (_go && ackWaitMS == UINT16_MAX && returnStatus == EXIT_FAILURE);

  return returnStatus;
}

/*-----------------------------------------------------------*/

int mqtt_client_subscribe(const MQTTSubscribeInfo_t *topics, size_t numTopics, uint16_t ackWaitMS) {
  return subscribe_command(true, topics, numTopics, ackWaitMS);
}

/*-----------------------------------------------------------*/

int mqtt_client_unsubscribe(const MQTTSubscribeInfo_t *topics, size_t numTopics, uint16_t ackWaitMS) {
  return subscribe_command(false, topics, numTopics, ackWaitMS);
}

/*-----------------------------------------------------------*/

int mqtt_client_publish(const MQTTPublishInfo_t *publishInfo, uint16_t ackWaitMS) {
  if (!mqtt_is_initialised()) {
    ESP_LOGE(TAG, "MQTT client is not running");
    return EXIT_FAILURE;
  }

  int returnStatus = EXIT_SUCCESS;
  MQTTStatus_t mqttStatus = MQTTStatus_t ::MQTTSuccess;
  uint16_t packetId = 0;

  LogInfo(("Publishing to %.*s", publishInfo->topicNameLength, publishInfo->pTopicName));
  do {
    xSemaphoreTake(ackMutex, portMAX_DELAY);
    {
      /* Send SUBSCRIBE packet. */
      xSemaphoreTake(mqttMutex, portMAX_DELAY);
      {
        if (_go) {

          packetId = MQTT_GetPacketId(&xMqttContext);

          /* Reset the ACK packet identifier being received, and number of expected status. */
          globalAckPacketIdentifier = 0;

          mqttStatus = MQTT_Publish(&xMqttContext,
                                    publishInfo,
                                    packetId);
        } else {
          returnStatus = EXIT_FAILURE;
        }
      }
      xSemaphoreGive(mqttMutex);

      if (mqttStatus != MQTTSuccess) {
        LogError(("Failed to send Publish packet to broker with error = %s.",
            MQTT_Status_strerror(mqttStatus)));
        returnStatus = EXIT_FAILURE;
      } else {
        returnStatus = waitForPacketAck(&xMqttContext, packetId,
                                        ackWaitMS == UINT16_MAX ? CONFIG_MQTT_ACK_TIMEOUT_MS : ackWaitMS);
      }
    }
    xSemaphoreGive(ackMutex);

    // Report on console
    if (returnStatus == EXIT_FAILURE) {
      LogError(("Publish failed to %.*s", publishInfo->topicNameLength, publishInfo->pTopicName));
    } else {
      LogInfo(("Publishing success to %.*s", publishInfo->topicNameLength, publishInfo->pTopicName));
    }

    // Don't kill CPU
    if (_go && returnStatus == EXIT_FAILURE) {
      vTaskDelay(CONNECTION_RETRY_BACKOFF_BASE_MS);
    }
  } while (_go && ackWaitMS == UINT16_MAX && returnStatus == EXIT_FAILURE);

  return returnStatus;
}

/*-----------------------------------------------------------*/

static void eventCallback(MQTTContext_t *pMqttContext,
                          MQTTPacketInfo_t *pPacketInfo,
                          MQTTDeserializedInfo_t *pDeserializedInfo) {
  uint16_t packetIdentifier;

  assert(pMqttContext != NULL);
  assert(pPacketInfo != NULL);
  assert(pDeserializedInfo != NULL);

  /* Suppress unused parameter warning when asserts are disabled in build. */
  (void) pMqttContext;

  packetIdentifier = pDeserializedInfo->packetIdentifier;

  if ((pPacketInfo->type & 0xF0U) == MQTT_PACKET_TYPE_PUBLISH) {
    /* Handle incoming publish. */
    SubscriptionManager_DispatchHandler(&xMqttContext, pDeserializedInfo->pPublishInfo);
  } else {
    /* Handle other packets. */
    switch (pPacketInfo->type) {
      case MQTT_PACKET_TYPE_SUBACK: {
        MQTT_GetSubAckStatusCodes(pPacketInfo, (uint8_t **) &globalSubAckStatus, &numSubAckStatus);
        globalAckPacketIdentifier = packetIdentifier;
        break;
      }
      case MQTT_PACKET_TYPE_UNSUBACK: {
        globalAckPacketIdentifier = packetIdentifier;
        break;
      }
      case MQTT_PACKET_TYPE_PINGRESP: {
        // Handled by underlying coreMQTT
        break;
      }
      case MQTT_PACKET_TYPE_PUBACK: {
        globalAckPacketIdentifier = packetIdentifier;
        break;
      }
      default: {
        /* Any other packet type is invalid. */
        LogError(("Unknown packet type received:(%02x).", pPacketInfo->type));
      }
    }
  }
}

/*-----------------------------------------------------------*/

static int establishMqttSession(MQTTContext_t *pMqttContext,
                                bool createCleanSession,
                                bool *pSessionPresent) {
  int returnStatus = EXIT_SUCCESS;
  MQTTStatus_t mqttStatus;
  MQTTConnectInfo_t connectInfo = {};

  assert(pMqttContext != NULL);
  assert(pSessionPresent != NULL);

  /* Establish MQTT session by sending a CONNECT packet. */

  /* If #createCleanSession is true, start with a clean session
   * i.e. direct the MQTT broker to discard any previous session data.
   * If #createCleanSession is false, directs the broker to attempt to
   * reestablish a session which was already present. */
  connectInfo.cleanSession = createCleanSession;

  /* The client identifier is used to uniquely identify this MQTT client to
   * the MQTT broker. In a production device the identifier can be something
   * unique, such as a device serial number. */
  connectInfo.pClientIdentifier = identity_thing_id();
  connectInfo.clientIdentifierLength = strlen(identity_thing_id());

  /* The maximum time interval in seconds which is allowed to elapse
   * between two Control Packets.
   * It is the responsibility of the Client to ensure that the interval between
   * Control Packets being sent does not exceed the this Keep Alive value. In the
   * absence of sending any other Control Packets, the Client MUST send a
   * PINGREQ Packet. */
  connectInfo.keepAliveSeconds = MQTT_KEEP_ALIVE_INTERVAL_SECONDS;

  /* Use the username and password for authentication, if they are defined.
   * Refer to the AWS IoT documentation below for details regarding client
   * authentication with a username and password.
   * https://docs.aws.amazon.com/iot/latest/developerguide/custom-authentication.html
   * An authorizer setup needs to be done, as mentioned in the above link, to use
   * username/password based client authentication.
   *
   * The username field is populated with voluntary metrics to AWS IoT.
   * The metrics collected by AWS IoT are the operating system, the operating
   * system's version, the hardware platform, and the MQTT Client library
   * information. These metrics help AWS IoT improve security and provide
   * better technical support.
   *
   * If client authentication is based on username/password in AWS IoT,
   * the metrics string is appended to the username to support both client
   * authentication and metrics collection. */
  connectInfo.pUserName = METRICS_STRING;
  connectInfo.userNameLength = METRICS_STRING_LENGTH;
  /* Password for authentication is not used. */
  connectInfo.pPassword = NULL;
  connectInfo.passwordLength = 0U;

  /* Send MQTT CONNECT packet to broker. */
  mqttStatus = MQTT_Connect(pMqttContext, &connectInfo, NULL, CONNACK_RECV_TIMEOUT_MS, pSessionPresent);

  if (mqttStatus != MQTTSuccess) {
    returnStatus = EXIT_FAILURE;
    LogError(("Connection with MQTT broker failed with status %s.",
        MQTT_Status_strerror(mqttStatus)));
  } else {
    LogInfo(("MQTT connection successfully established with broker."));
  }

  return returnStatus;
}

/*-----------------------------------------------------------*/

static int connectToServerWithBackoffRetries(NetworkContext_t *pNetworkContext,
                                             MQTTContext_t *pMqttContext,
                                             bool *pClientSessionPresent,
                                             bool *pBrokerSessionPresent) {
  int returnStatus = EXIT_FAILURE;
  BackoffAlgorithmStatus_t backoffAlgStatus = BackoffAlgorithmSuccess;
  BackoffAlgorithmContext_t reconnectParams;
  bool createCleanSession;

  uint16_t nextRetryBackOff;

  /* Initialize reconnect attempts and interval */
  BackoffAlgorithm_InitializeParams(&reconnectParams,
                                    CONNECTION_RETRY_BACKOFF_BASE_MS,
                                    CONNECTION_RETRY_MAX_BACKOFF_DELAY_MS,
                                    BACKOFF_ALGORITHM_RETRY_FOREVER);

  /* Attempt to connect to MQTT broker. If connection fails, retry after
   * a timeout. Timeout value will exponentially increase until maximum
   * attempts are reached.
   */
  do {
    /* Establish a TLS session with the MQTT broker. This example connects
     * to the MQTT broker as specified in AWS_IOT_ENDPOINT and AWS_MQTT_PORT
     * at the demo config header. */
    LogInfo(("Establishing a TLS session to %s.", identity_get()->ats_ep));
    xSemaphoreTake(mqttMutex, portMAX_DELAY);

    // Free memory associated with previous connection, if any
    xTlsDisconnect(pNetworkContext);

    if (xTlsConnect(pNetworkContext) == TLS_TRANSPORT_SUCCESS) {
      /* A clean MQTT session needs to be created, if there is no session saved
       * in this MQTT client. */
      createCleanSession = (*pClientSessionPresent == true) ? false : true;

      /* Sends an MQTT Connect packet using the established TLS session,
       * then waits for connection acknowledgment (CONNACK) packet. */
      returnStatus = establishMqttSession(pMqttContext, createCleanSession, pBrokerSessionPresent);
    }

    xSemaphoreGive(mqttMutex);

    if (returnStatus == EXIT_FAILURE) {
      /* Generate a random number and get back-off value (in milliseconds) for the next connection retry. */
      backoffAlgStatus = BackoffAlgorithm_GetNextBackoff(&reconnectParams, rand(), &nextRetryBackOff);

      if (backoffAlgStatus == BackoffAlgorithmRetriesExhausted) {
        LogError(("Connection to the broker failed, all attempts exhausted."));
        returnStatus = EXIT_FAILURE;
      } else if (backoffAlgStatus == BackoffAlgorithmSuccess) {
        LogWarn(("Connection to the broker failed. Retrying connection "
                 "after %hu ms backoff.",
            (unsigned short) nextRetryBackOff));
        Clock_SleepMs(nextRetryBackOff);
      }
    }
  } while ((returnStatus == EXIT_FAILURE) && (backoffAlgStatus == BackoffAlgorithmSuccess) && _go);

  // Free up resources left behind by last attempt to connect
  if (returnStatus == EXIT_FAILURE) {
    xSemaphoreTake(mqttMutex, portMAX_DELAY);
    (void) xTlsDisconnect(pNetworkContext);
    xSemaphoreGive(mqttMutex);
  }

  return returnStatus;
}

/*-----------------------------------------------------------*/

static void prvMQTTClientTask(void *pvParameters) {
  ESP_LOGI(TAG, "Starting MQTT Client loop");

  bool clientSessionPresent = false, brokerSessionPresent = false;

  do {
    /* Wait for the device to be connected */
    xEventGroupWaitBits(s_networkEventGroup,
                        SNTP_TIME_SYNCED_BIT | WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY);

    if (!(xEventGroupGetBits(s_networkEventGroup) & CORE_MQTT_CLIENT_CONNECTED_BIT)) {
      auto returnStatus = connectToServerWithBackoffRetries(&xNetworkContext, &xMqttContext, &clientSessionPresent,
                                                            &brokerSessionPresent);
      if (returnStatus == EXIT_SUCCESS && xMqttContext.connectStatus == MQTTConnected) {
        LogInfo(("--> MQTT broker Connected"));
        xEventGroupSetBits(s_networkEventGroup, CORE_MQTT_CLIENT_CONNECTED_BIT);
        esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_CONNECTED_EVENT, NULL, 0, portMAX_DELAY);
      }
    }

    // Yay, then we can do loop processing
    if (_go && xEventGroupGetBits(s_networkEventGroup) & CORE_MQTT_CLIENT_CONNECTED_BIT) {

      xSemaphoreTake(mqttMutex, portMAX_DELAY);
      MQTTStatus_t status = MQTT_ProcessLoop(&xMqttContext);
      xSemaphoreGive(mqttMutex);
      portYIELD();
      vTaskDelay(1);  // Without this call, other tasks don't get a chance to acquire mqttMutex

      if (xMqttContext.connectStatus != MQTTConnected || status == MQTTRecvFailed || status == MQTTSendFailed) {
        // Then we have a disconnect
        if (xEventGroupGetBits(s_networkEventGroup) & CORE_MQTT_CLIENT_CONNECTED_BIT) {
          ESP_LOGW(TAG, "--> MQTT broker disconnected");
          xEventGroupClearBits(s_networkEventGroup, CORE_MQTT_CLIENT_CONNECTED_BIT);
          esp_event_post(CORE_MQTT_EVENT, CORE_MQTT_DISCONNECTED_EVENT, NULL, 0, portMAX_DELAY);
        }
      }
    }
  } while (_go);

  ESP_LOGI(TAG, "Stopped MQTT Client loop");
  mqtt_loop_task = nullptr;
  vTaskDelete(nullptr);
}

/*-----------------------------------------------------------*/

esp_err_t mqtt_client_init(EventGroupHandle_t networkEventGroup) {
  esp_err_t returnStatus = ESP_FAIL;
  if (mqtt_is_initialised()) {
    ESP_LOGE(TAG, "MQTT client is already running");
    return returnStatus;
  }

  ESP_LOGI(TAG, "Initialising");
  s_networkEventGroup = networkEventGroup;

  // Set initial state as disconnected
  xEventGroupClearBits(networkEventGroup, CORE_MQTT_CLIENT_CONNECTED_BIT);

  ESP_LOGI(TAG, "Initialising for ATS endpoint: %s", identity_get()->ats_ep);
  BaseType_t xResult = pdFAIL;

  xResult = prvInitializeNetworkContext();
  if (xResult != pdPASS) {
    ESP_LOGE(TAG, "Failed to initialize network context");
    configASSERT(xResult == pdPASS);
  } else {
    // Mutex to protect MQTT loop and calls
    TransportInterface_t transport = {};
    transport.pNetworkContext = &xNetworkContext;
    transport.send = espTlsTransportSend;
    transport.recv = espTlsTransportRecv;
    transport.writev = nullptr;

    /* Fill the values for network buffer. */
    networkBuffer.pBuffer = buffer;
    networkBuffer.size = NETWORK_BUFFER_SIZE;

    /* Initialize MQTT library. */
    MQTTStatus_t status = MQTT_Init(&xMqttContext,
                                    &transport,
                                    Clock_GetTimeMs,
                                    eventCallback,
                                    &networkBuffer);

    if (status != MQTTSuccess) {
      ESP_LOGE(TAG, "Failed to initialize and start coreMQTT-Agent network manager.");
      configASSERT(xResult == MQTTSuccess);
    } else {
      status = MQTT_InitStatefulQoS(&xMqttContext,
                                    pOutgoingPublishRecords,
                                    OUTGOING_PUBLISH_RECORD_LEN,
                                    pIncomingPublishRecords,
                                    INCOMING_PUBLISH_RECORD_LEN);

      if (status != MQTTSuccess) {
        LogError(("MQTT_InitStatefulQoS failed: Status = %s.", MQTT_Status_strerror(status)));
        configASSERT(xResult == MQTTSuccess);
      } else {
        // Cool now we can start the mqtt loop
        _go = true;
        if (xTaskCreate(prvMQTTClientTask,
                        "coreMQTT-Client",
                        configMQTT_TASK_STACK_SIZE,
                        nullptr,
                        configMQTT_TASK_PRIORITY,
                        &mqtt_loop_task) != pdPASS) {
          ESP_LOGE(TAG, "Failed to create coreMQTT-client task.");
        } else {
          ESP_LOGI(TAG, "MQTT initialised");
          returnStatus = ESP_OK;
        }
      }
    }
  }

  return returnStatus;
}

/*-----------------------------------------------------------*/

int mqtt_client_disconnect() {
  if (!mqtt_is_initialised()) {
    ESP_LOGE(TAG, "MQTT client is not running");
    return EXIT_FAILURE;
  }

  ESP_LOGI(TAG, "Stopping MQTT client");
  MQTTStatus_t mqttStatus = MQTTSuccess;
  int returnStatus = EXIT_SUCCESS;

  /* Send DISCONNECT. */
  xSemaphoreTake(mqttMutex, portMAX_DELAY);
  {
    mqttStatus = MQTT_Disconnect(&xMqttContext);

    if (mqttStatus != MQTTSuccess) {
      LogError(("Sending MQTT DISCONNECT failed with status=%s.",
          MQTT_Status_strerror(mqttStatus)));
      returnStatus = EXIT_FAILURE;
    }

    // Wait for MQTT loop to terminate
    _go = false;
  }
  xSemaphoreGive(mqttMutex);

  do {
    vTaskDelay(pdMS_TO_TICKS(100));
  } while(mqtt_loop_task != nullptr);

  // Free memory associated with previous connection, if any
  xTlsDisconnect(&xNetworkContext);
  vSemaphoreDelete(xNetworkContext.xTlsContextSemaphore);

  return returnStatus;
}