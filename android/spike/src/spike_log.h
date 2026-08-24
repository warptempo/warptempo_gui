// Logging for the M2 spike. logcat only; the spike has no other diagnostic road.
#pragma once

#include <android/log.h>

#define SPIKE_TAG "warptempo_spike"
#define SPIKE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPIKE_TAG, __VA_ARGS__)
#define SPIKE_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPIKE_TAG, __VA_ARGS__)
#define SPIKE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SPIKE_TAG, __VA_ARGS__)
