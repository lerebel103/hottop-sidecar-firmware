/*
 * FreeRTOS V202011.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://aws.amazon.com/freertos
 *
 */

/**
 * @file subscription_manager.c
 * @brief Functions for managing MQTT subscriptions.
 */

/* Standard includes. */
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

/* Subscription manager header include. */
#include "subscription_manager.h"
#include "core_mqtt_agent.h"
#include "events_common.h"


/**
 * @brief Defines the structure to use as the command callback context.
 */
struct MQTTAgentCommandContext
{
  MQTTStatus_t xReturnStatus;
  TaskHandle_t xTaskToNotify;
  uint32_t ulNotificationValue;
  IncomingPubCallback_t pxIncomingPublishCallback;
  void * pArgs;
};

/**
 * @brief Static handle used for MQTT agent context.
 */
extern MQTTAgentContext_t xGlobalMqttAgentContext;

/**
 * @brief The event group used to manage network events.
 */
static EventGroupHandle_t xNetworkEventGroup;

bool addSubscription( SubscriptionElement_t * pxSubscriptionList,
                      const char * pcTopicFilterString,
                      uint16_t usTopicFilterLength,
                      IncomingPubCallback_t pxIncomingPublishCallback,
                      void * pvIncomingPublishCallbackContext )
{
    int32_t lIndex = 0;
    size_t xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
    bool xReturnStatus = false;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) ||
        ( pxIncomingPublishCallback == NULL ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                    " usTopicFilterLength=%u, pxIncomingPublishCallback=%p.",
                    pxSubscriptionList,
                    pcTopicFilterString,
                    ( unsigned int ) usTopicFilterLength,
                    pxIncomingPublishCallback ) );
    }
    else
    {
        /* Start at end of array, so that we will insert at the first available index.
         * Scans backwards to find duplicates. */
        for( lIndex = ( int32_t ) SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS - 1; lIndex >= 0; lIndex-- )
        {
            if( pxSubscriptionList[ lIndex ].usFilterStringLength == 0 )
            {
                xAvailableIndex = lIndex;
            }
            else if( ( pxSubscriptionList[ lIndex ].usFilterStringLength == usTopicFilterLength ) &&
                     ( strncmp( pcTopicFilterString, pxSubscriptionList[ lIndex ].pcSubscriptionFilterString, ( size_t ) usTopicFilterLength ) == 0 ) )
            {
                /* If a subscription already exists, don't do anything. */
                if( ( pxSubscriptionList[ lIndex ].pxIncomingPublishCallback == pxIncomingPublishCallback ) &&
                    ( pxSubscriptionList[ lIndex ].pvIncomingPublishCallbackContext == pvIncomingPublishCallbackContext ) )
                {
                    LogWarn( ( "Subscription already exists.\n" ) );
                    xAvailableIndex = SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS;
                    xReturnStatus = true;
                    break;
                }
            }
        }

        if( xAvailableIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS )
        {
            pxSubscriptionList[ xAvailableIndex ].pcSubscriptionFilterString = pcTopicFilterString;
            pxSubscriptionList[ xAvailableIndex ].usFilterStringLength = usTopicFilterLength;
            pxSubscriptionList[ xAvailableIndex ].pxIncomingPublishCallback = pxIncomingPublishCallback;
            pxSubscriptionList[ xAvailableIndex ].pvIncomingPublishCallbackContext = pvIncomingPublishCallbackContext;
            xReturnStatus = true;
        }
    }

    return xReturnStatus;
}

/*-----------------------------------------------------------*/

void removeSubscription( SubscriptionElement_t * pxSubscriptionList,
                         const char * pcTopicFilterString,
                         uint16_t usTopicFilterLength )
{
    uint32_t ulIndex = 0;

    if( ( pxSubscriptionList == NULL ) ||
        ( pcTopicFilterString == NULL ) ||
        ( usTopicFilterLength == 0U ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pcTopicFilterString=%p,"
                    " usTopicFilterLength=%u.",
                    pxSubscriptionList,
                    pcTopicFilterString,
                    ( unsigned int ) usTopicFilterLength ) );
    }
    else
    {
        for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
        {
            if( pxSubscriptionList[ ulIndex ].usFilterStringLength == usTopicFilterLength )
            {
                if( strncmp( pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString, pcTopicFilterString, usTopicFilterLength ) == 0 )
                {
                    memset( &( pxSubscriptionList[ ulIndex ] ), 0x00, sizeof( SubscriptionElement_t ) );
                }
            }
        }
    }
}

/*-----------------------------------------------------------*/

bool handleIncomingPublishes( SubscriptionElement_t * pxSubscriptionList,
                              MQTTPublishInfo_t * pxPublishInfo )
{
    uint32_t ulIndex = 0;
    bool isMatched = false, publishHandled = false;

    if( ( pxSubscriptionList == NULL ) ||
        ( pxPublishInfo == NULL ) )
    {
        LogError( ( "Invalid parameter. pxSubscriptionList=%p, pxPublishInfo=%p,",
                    pxSubscriptionList,
                    pxPublishInfo ) );
    }
    else
    {
        for( ulIndex = 0U; ulIndex < SUBSCRIPTION_MANAGER_MAX_SUBSCRIPTIONS; ulIndex++ )
        {
            if( pxSubscriptionList[ ulIndex ].usFilterStringLength > 0 )
            {
                MQTT_MatchTopic( pxPublishInfo->pTopicName,
                                 pxPublishInfo->topicNameLength,
                                 pxSubscriptionList[ ulIndex ].pcSubscriptionFilterString,
                                 pxSubscriptionList[ ulIndex ].usFilterStringLength,
                                 &isMatched );

                if( isMatched == true )
                {
                    pxSubscriptionList[ ulIndex ].pxIncomingPublishCallback( pxSubscriptionList[ ulIndex ].pvIncomingPublishCallbackContext,
                                                                             pxPublishInfo );
                    publishHandled = true;
                }
            }
        }
    }

    return publishHandled;
}

static void prvSubscribeCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                         MQTTAgentReturnInfo_t * pxReturnInfo )
{
  bool xSubscriptionAdded = false;
  MQTTAgentSubscribeArgs_t * pxSubscribeArgs = ( MQTTAgentSubscribeArgs_t * ) pxCommandContext->pArgs;

  /* Store the result in the application defined context so the task that
   * initiated the subscribe can check the operation's status.  Also send the
   * status as the notification value.  These things are just done for
   * demonstration purposes. */
  pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

  /* Check if the subscribe operation is a success. Only one topic is
   * subscribed by this demo. */
  if( pxReturnInfo->returnCode == MQTTSuccess )
  {
    /* Add subscription so that incoming publishes are routed to the application
     * callback. */
    xSubscriptionAdded = addSubscription( ( SubscriptionElement_t * ) xGlobalMqttAgentContext.pIncomingCallbackContext,
                                          pxSubscribeArgs->pSubscribeInfo->pTopicFilter,
                                          pxSubscribeArgs->pSubscribeInfo->topicFilterLength,
                                          pxCommandContext->pxIncomingPublishCallback,
                                          NULL );

    if( xSubscriptionAdded == false )
    {
      LogError( ("Failed to register an incoming publish callback for topic %.*s.",
                pxSubscribeArgs->pSubscribeInfo->topicFilterLength,
                pxSubscribeArgs->pSubscribeInfo->pTopicFilter) );
    }
  }

  xTaskNotify( pxCommandContext->xTaskToNotify,
               ( uint32_t ) ( pxReturnInfo->returnCode ),
               eSetValueWithOverwrite );
}

static BaseType_t prvWaitForCommandAcknowledgment( uint32_t * pulNotifiedValue )
{
  BaseType_t xReturn;

  /* Wait for this task to get notified, passing out the value it gets
   * notified with. */
  xReturn = xTaskNotifyWait( 0,
                             0,
                             pulNotifiedValue,
                             portMAX_DELAY );
  return xReturn;
}

bool mqttManagerSubscribeToTopic( MQTTQoS_t xQoS, const char * pcTopicFilter, IncomingPubCallback_t pxIncomingPublishCallback)
{
  MQTTStatus_t xCommandAdded;
  BaseType_t xCommandAcknowledged = pdFALSE;
  MQTTAgentSubscribeArgs_t xSubscribeArgs;
  MQTTSubscribeInfo_t xSubscribeInfo;
  static int32_t ulNextSubscribeMessageID = 0;
  MQTTAgentCommandContext_t xApplicationDefinedContext = {  };
  MQTTAgentCommandInfo_t xCommandParams = { 0UL };

  /* Create a unique number of the subscribe that is about to be sent.  The number
   * is used as the command context and is sent back to this task as a notification
   * in the callback that executed upon receipt of the subscription acknowledgment.
   * That way this task can match an acknowledgment to a subscription. */
  xTaskNotifyStateClear( NULL );

  ulNextSubscribeMessageID++;

  /* Complete the subscribe information.  The topic string must persist for
   * duration of subscription! */
  xSubscribeInfo.pTopicFilter = pcTopicFilter;
  xSubscribeInfo.topicFilterLength = ( uint16_t ) strlen( pcTopicFilter );
  xSubscribeInfo.qos = xQoS;
  xSubscribeArgs.pSubscribeInfo = &xSubscribeInfo;
  xSubscribeArgs.numSubscriptions = 1;

  /* Complete an application defined context associated with this subscribe message.
   * This gets updated in the callback function so the variable must persist until
   * the callback executes. */
  xApplicationDefinedContext.ulNotificationValue = ulNextSubscribeMessageID;
  xApplicationDefinedContext.xTaskToNotify = xTaskGetCurrentTaskHandle();
  xApplicationDefinedContext.pxIncomingPublishCallback = pxIncomingPublishCallback;
  xApplicationDefinedContext.pArgs = ( void * ) &xSubscribeArgs;

  xCommandParams.blockTimeMs = CONFIG_GRI_MQTT_MAX_SEND_BLOCK_TIME_MS;
  xCommandParams.cmdCompleteCallback = prvSubscribeCommandCallback;
  xCommandParams.pCmdCompleteCallbackContext = &xApplicationDefinedContext;;

  /* Loop in case the queue used to communicate with the MQTT agent is full and
   * attempts to post to it time out.  The queue will not become full if the
   * priority of the MQTT agent task is higher than the priority of the task
   * calling this function. */
  LogInfo((
            "Sending subscribe request to agent for topic filter: %s with id %d",
            pcTopicFilter,
            ( int ) ulNextSubscribeMessageID) );

  do
  {
    xCommandAdded = MQTTAgent_Subscribe( &xGlobalMqttAgentContext,
                                         &xSubscribeArgs,
                                         &xCommandParams );
  } while( xCommandAdded != MQTTSuccess );

  /* Wait for acks to the subscribe message - this is optional but done here
   * so the code below can check the notification sent by the callback matches
   * the ulNextSubscribeMessageID value set in the context above. */
  xCommandAcknowledged = prvWaitForCommandAcknowledgment( NULL );

  /* Check both ways the status was passed back just for demonstration
   * purposes. */
  if( ( xCommandAcknowledged != pdTRUE ) ||
      ( xApplicationDefinedContext.xReturnStatus != MQTTSuccess ) )
  {
    LogError((
              "Error or timed out waiting for ack to subscribe message topic %s",
              pcTopicFilter) );
  }
  else
  {
    LogInfo((
              "Received subscribe ack for topic %s containing ID %d",
              pcTopicFilter,
              ( int ) xApplicationDefinedContext.ulNotificationValue) );
  }

  return xCommandAcknowledged;
}

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo )
{
  /* Store the result in the application defined context so the task that
   * initiated the publish can check the operation's status. */
  pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

  if( pxCommandContext->xTaskToNotify != NULL )
  {
    /* Send the context's ulNotificationValue as the notification value so
     * the receiving task can check the value it set in the context matches
     * the value it receives in the notification. */
    xTaskNotify( pxCommandContext->xTaskToNotify,
                 pxCommandContext->ulNotificationValue,
                 eSetValueWithOverwrite );
  }
}

MQTTStatus_t mqttPublishMessage(const char* topic, const char* msg, size_t len, MQTTQoS_t qos, bool wait) {
  MQTTPublishInfo_t xPublishInfo = {  };
  uint32_t ulNotification = 0U, ulValueToNotify = 0UL;

  /* Configure the publish operation. */
  memset( ( void * ) &xPublishInfo, 0x00, sizeof( xPublishInfo ) );
  xPublishInfo.qos = qos;
  xPublishInfo.pTopicName = topic;
  xPublishInfo.topicNameLength = ( uint16_t ) strlen(  xPublishInfo.pTopicName );
  xPublishInfo.pPayload = msg;
  xPublishInfo.payloadLength = ( uint16_t ) strlen( (char*)xPublishInfo.pPayload );

  MQTTAgentCommandContext_t xCommandContext = {};
  /* Store the handler to this task in the command context so the callback
   * that executes when the command is acknowledged can send a notification
   * back to this task. */
  memset( ( void * ) &xCommandContext, 0x00, sizeof( xCommandContext ) );
  xCommandContext.xTaskToNotify = xTaskGetCurrentTaskHandle();

  MQTTAgentCommandInfo_t xCommandParams = { 0UL };
  xCommandParams.blockTimeMs = CONFIG_GRI_MQTT_MAX_SEND_BLOCK_TIME_MS;
  xCommandParams.cmdCompleteCallback = prvPublishCommandCallback;
  xCommandParams.pCmdCompleteCallbackContext = &xCommandContext;
  /* Also store the incrementing number in the command context so it can
   * be accessed by the callback that executes when the publish operation
   * is acknowledged. */
  xCommandContext.ulNotificationValue = ulValueToNotify;

  /* To ensure ulNotification doesn't accidentally hold the expected value
  * as it is to be checked against the value sent from the callback.. */
  ulNotification = ~ulValueToNotify;

  /* Wait for coreMQTT-Agent task to have working network connection and
   * not be performing an OTA update. */
  xEventGroupWaitBits( xNetworkEventGroup,
                       CORE_MQTT_AGENT_CONNECTED_BIT | CORE_MQTT_AGENT_OTA_NOT_IN_PROGRESS_BIT,
                       pdFALSE,
                       pdTRUE,
                       portMAX_DELAY );

  MQTTStatus_t xCommandAdded = MQTTAgent_Publish(&xGlobalMqttAgentContext, &xPublishInfo, &xCommandParams);
  configASSERT( xCommandAdded == MQTTSuccess );

  if (wait) {
    prvWaitForCommandAcknowledgment(&ulNotification);

    /* The value received by the callback that executed when the publish was
   * acked came from the context passed into MQTTAgent_Publish() above, so
   * should match the value set in the context above. */
    if (ulNotification != ulValueToNotify) {
      LogError((
                   "Timed out Rx'ing %s from Tx to %s",
                       (qos == 0) ? "completion notification for QoS0 publish" : "ack for QoS1 publish",
                       topic));

      return MQTTSendFailed;
    }
  }

  return MQTTSuccess;
}

void mqttManagerPubSubInit(EventGroupHandle_t networkEventGroup) {
  xNetworkEventGroup = networkEventGroup;
}
