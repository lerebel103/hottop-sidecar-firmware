#pragma once

#include <cstdint>


void app_metrics_send(char* buffer, size_t max_len);

bool app_metrics_update_required(int interval_sec);

void app_metrics_init();

