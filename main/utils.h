#pragma once

esp_err_t utils_load_from_nvs(const char* ns, const char* key, void* ptr, size_t size_in);
esp_err_t utils_save_to_nvs(const char* ns, const char* key, void* ptr, size_t size);