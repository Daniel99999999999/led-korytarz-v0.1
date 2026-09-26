#pragma once
template <class... T> inline void hostLog(T...) {}
#define ESP_LOGI(...) hostLog(__VA_ARGS__)
#define ESP_LOGW(...) hostLog(__VA_ARGS__)
#define ESP_LOGE(...) hostLog(__VA_ARGS__)

#include <cstdarg>
using vprintf_like_t = int (*)(const char*, va_list);
inline vprintf_like_t fakeLogOutput = nullptr;
inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t output) {
    const auto old = fakeLogOutput;
    fakeLogOutput = output;
    return old;
}
