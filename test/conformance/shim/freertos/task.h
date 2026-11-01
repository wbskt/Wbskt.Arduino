#pragma once

#include "FreeRTOS.h"

// stackDepth is in bytes, as on ESP-IDF.
BaseType_t xTaskCreate(TaskFunction_t task, const char* name, uint32_t stackDepth, void* argument,
                       UBaseType_t priority, TaskHandle_t* handle);
void vTaskDelete(TaskHandle_t task);
