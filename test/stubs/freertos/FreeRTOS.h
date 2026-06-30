// Minimal host-side FreeRTOS stub for native unit tests.
// Just the types/macros command_queue.h names. NOT a scheduler.
#ifndef LUME_TEST_FREERTOS_STUB_H
#define LUME_TEST_FREERTOS_STUB_H

#include <cstdint>
#include <cstddef>

typedef int          BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t     TickType_t;

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)

#endif // LUME_TEST_FREERTOS_STUB_H
