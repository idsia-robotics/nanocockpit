/*
 * trace_buffer.h
 * Elia Cereda <elia.cereda@idsia.ch>
 *
 * Copyright (C) 2022-2025 IDSIA, USI-SUPSI
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * CIRCULAR BUFFER TRACING
 *
 * Low-overhead event tracing over a circular buffer that can be dumped
 * 
 */

#ifndef __TRACE_BUFFER_H__
#define __TRACE_BUFFER_H__

#include "config.h"
#include "soc.h"
#include "utils.h"

#include <esp_ipc.h>
#include <esp_timer.h>
#include <perfmon.h>

#include <stdbool.h>
#include <stdint.h>

#define TRACE_NUM_CORES             (SOC_CPU_CORES_NUM)
#define TRACE_EVENTS_BUFFER         (3072)
#define TRACE_EVENTS_PERF_COUNTER   (XTPERF_CNT_CYCLES)

typedef enum trace_evt_e {
    TRACE_EVT_SYNC = 0x0,

    TRACE_EVT_CPX_TCP_CONNECTION = 0x10,
    TRACE_EVT_CPX_TCP_SEND       = 0x11,
    TRACE_EVT_CPX_TCP_RECEIVE    = 0x12,
    TRACE_EVT_CPX_UDP_SEND       = 0x13,
    TRACE_EVT_CPX_UDP_RECEIVE    = 0x14,

    TRACE_EVT_CPX_SPI_IDLE       = 0x20,
    TRACE_EVT_CPX_SPI_TRANSFER   = 0x21,
    TRACE_EVT_CPX_SPI_GAP_RTT    = 0x22
} __attribute__((packed)) trace_evt_e;

typedef enum trace_state_e {
    TRACE_MARKER,
    TRACE_BEGIN,
    TRACE_END,
} __attribute__((packed)) trace_state_e;

typedef struct trace_evt_s {
    union {
        uint64_t data;
        struct {
            trace_evt_e event;
            trace_state_e state;
            uint16_t context;
            uint32_t perf_counter;
        };
    };
} trace_evt_t;

typedef struct trace_buffer_s {
    bool started;
    int next_event;
    int event_count;
    trace_evt_t buffer[TRACE_EVENTS_BUFFER];
} trace_buffer_t;

void trace_buffer_init_all();

void trace_buffer_init(int core_id);
void trace_buffer_start();

void trace_buffer_dump();

// Record an event
static inline void trace_event(trace_evt_e event, trace_state_e state, uint16_t context);
static inline void trace_event_from_isr(trace_evt_e event, trace_state_e state, uint16_t context);

// Record a sync event with a global timestamp
static inline void trace_sync();
static inline void trace_sync_all();

// Implementation

extern trace_buffer_t *trace_buffers[TRACE_NUM_CORES];

static inline void trace_push_event(trace_evt_t event) {
    int core_id = soc_core_id();
    trace_buffer_t *t = trace_buffers[core_id];

    if (t == NULL) {
        ASSERTION_FAILURE("Trace buffer for core %d not initialized\n", core_id);
    } else if (!t->started) {
        ASSERTION_FAILURE("Trace buffer for core %d not started\n", core_id);
    }

    t->buffer[t->next_event] = event;
    t->next_event = (t->next_event + 1) % TRACE_EVENTS_BUFFER;
    t->event_count = MIN(t->event_count + 1, TRACE_EVENTS_BUFFER);
}

static inline void trace_event(trace_evt_e event, trace_state_e state, uint16_t context) {
    uint32_t perf_counter = xtensa_perfmon_value(TRACE_EVENTS_PERF_COUNTER);
    trace_push_event((trace_evt_t){
        .event = event, .state = state, .context = context, .perf_counter = perf_counter
    });
}

static inline void trace_event_from_isr(trace_evt_e event, trace_state_e state, uint16_t context) {
    // TODO: check implementation is ISR safe
    trace_event(event, state, context);
}

static inline void trace_sync() {
    xtensa_perfmon_stop();
    uint32_t time = esp_timer_get_time();
    uint16_t time_lo = (time >>  0) & 0xffff;
    uint16_t time_hi = (time >> 16) & 0xffff;
    trace_event(TRACE_EVT_SYNC,  TRACE_BEGIN, time_lo);

    xtensa_perfmon_reset(TRACE_EVENTS_PERF_COUNTER);
    xtensa_perfmon_start();
    trace_event(TRACE_EVT_SYNC, TRACE_END, time_hi);
}

void trace_sync_all() {
    for (int i = 0; i < TRACE_NUM_CORES; i++) {
        esp_ipc_call_blocking(i, trace_sync, NULL);
    }
}

#endif // __TRACE_BUFFER_H__
