#pragma once

#include "network_transport.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

  /* Note: what is here was re-written to remove semaphore locking.
   *
   * The original implementation from Espressif located under coreMQTT/port is
   * rather unworkable in practice since it is single-duplex. Reads and Writes are
   * protected by the same mutex, which means that no Tx can take place whilst
   * in an Rx call, which is almost always since these are long polls with timeout.
   *
   * Instead, we rely on the mqtt wrapper client code to handle thread safety.
   */

TlsTransportStatus_t xTlsConnectNoThread(NetworkContext_t* pxNetworkContext );

TlsTransportStatus_t xTlsDisconnectNoThread( NetworkContext_t* pxNetworkContext );

int32_t espTlsTransportSendNoThread( NetworkContext_t* pxNetworkContext,
                             const void* pvData, size_t uxDataLen );

int32_t espTlsTransportRecvNoThread( NetworkContext_t* pxNetworkContext,
                             void* pvData, size_t uxDataLen );

/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */
