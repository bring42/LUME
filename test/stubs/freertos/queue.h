// Minimal host-side FreeRTOS queue stub for native unit tests.
// A real (single-threaded) in-memory FIFO so CommandQueue actually works on
// host — enough for logic tests; no locking, since tests are single-threaded.
#ifndef LUME_TEST_FREERTOS_QUEUE_STUB_H
#define LUME_TEST_FREERTOS_QUEUE_STUB_H

#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <vector>

struct QueueDef {
    size_t itemSize;
    size_t capacity;
    std::deque<std::vector<uint8_t>> items;
};
typedef QueueDef* QueueHandle_t;

inline QueueHandle_t xQueueCreate(size_t length, size_t itemSize) {
    return new QueueDef{itemSize, length, {}};
}

inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    if (!q || q->items.size() >= q->capacity) return pdFALSE;
    const uint8_t* p = static_cast<const uint8_t*>(item);
    q->items.emplace_back(p, p + q->itemSize);
    return pdTRUE;
}

inline BaseType_t xQueueReceive(QueueHandle_t q, void* out, TickType_t) {
    if (!q || q->items.empty()) return pdFALSE;
    std::memcpy(out, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {
    return q ? static_cast<UBaseType_t>(q->items.size()) : 0;
}

inline void xQueueReset(QueueHandle_t q) {
    if (q) q->items.clear();
}

#endif // LUME_TEST_FREERTOS_QUEUE_STUB_H
