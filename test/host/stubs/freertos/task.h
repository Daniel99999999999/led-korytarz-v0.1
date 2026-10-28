#pragma once
using TaskHandle_t = void*;
inline int fakeTaskToken = 0;
inline TaskHandle_t fakeTask = &fakeTaskToken;
inline bool fakeIsr = false;
inline TaskHandle_t xTaskGetCurrentTaskHandle() {
    return fakeTask;
}
inline bool xPortInIsrContext() {
    return fakeIsr;
}

inline unsigned fakeStackFree = 2048;
inline unsigned fakeStackSamples = 0;
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t) {
    ++fakeStackSamples;
    return fakeStackFree;
}
