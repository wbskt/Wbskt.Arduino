// The little of FreeRTOS the client uses, over std::thread.
#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

#define pdPASS 1
