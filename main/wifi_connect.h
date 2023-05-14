#pragma once

#include <esp_bit_defs.h>

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_EVENT = BIT0;

void wifi_connect_init();