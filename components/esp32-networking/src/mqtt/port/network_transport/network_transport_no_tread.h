#pragma once

#include "network_transport.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */


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
