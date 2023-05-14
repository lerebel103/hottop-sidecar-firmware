#pragma once

#include <esp_bit_defs.h>

/* SNTP sync bit */
const int TIME_SYNC_BIT = BIT1;

#define DEFAULT_SYSTEM_TZ "AWST-8"

void sntp_sync_init(const char* primary_server = nullptr);

void sntp_set_system_tz(const char* tz);
const char* sntp_get_system_tz();

int sntp_sync_get_sync_duration();