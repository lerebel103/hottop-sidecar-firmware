#pragma once

bool app_config_update_required();

void app_config_update_send(char* payload, size_t max_len);

void app_config_init();