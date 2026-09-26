#pragma once
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_ARG = 1, ESP_ERR_INVALID_STATE = 2,
              ESP_ERR_NOT_FOUND = 3, ESP_ERR_INVALID_RESPONSE = 4, ESP_ERR_INVALID_CRC = 5,
              ESP_ERR_TIMEOUT = 6;
inline const char* esp_err_to_name(int) {
    return "fake error";
}
