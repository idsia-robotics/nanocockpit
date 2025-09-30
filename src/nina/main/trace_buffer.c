/*
 * trace_buffer.c
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
 * 
 * This software is based on the following publication:
 *    E. Cereda, A. Giusti, D. Palossi. "NanoCockpit: Performance-optimized 
 *    Application Framework for AI-based Autonomous Nanorobotics"
 * We kindly ask for a citation if you use in academic work.
 */

#include "trace_buffer.h"

#include <driver/gpio.h>

#include <string.h>

trace_buffer_t *trace_buffers[TRACE_NUM_CORES] = {};

void trace_buffer_init(int core_id) {
    if (trace_buffers[core_id] != NULL) {
        ASSERTION_FAILURE("Trace buffer for core %d already initialized\n", core_id);
    }
    
    trace_buffer_t *buffer = malloc(sizeof(trace_buffer_t));

    if (buffer == NULL) {
        ASSERTION_FAILURE("Failed to allocate memory for trace buffer for core %d\n", core_id);
    }

    memset(buffer, 0xaa, sizeof(trace_buffer_t));
    buffer->started = false;
    buffer->next_event = 0;
    buffer->event_count = 0;

    trace_buffers[core_id] = buffer;
}

void trace_buffer_start() {
    int core_id = soc_core_id();
    trace_buffer_t *t = trace_buffers[core_id];
    if (t == NULL) {
        ASSERTION_FAILURE("Trace buffer for core %d not initialized\n", core_id);
    }

    if (!t->started) {
        xtensa_perfmon_stop();
        xtensa_perfmon_init(TRACE_EVENTS_PERF_COUNTER, XTPERF_CNT_CYCLES, XTPERF_MASK_CYCLES, 0, -1);
        xtensa_perfmon_start();

        t->started = true;
    }

    trace_sync();
}

void trace_buffer_dump_core(int core_id) {
    trace_buffer_t *t = trace_buffers[core_id];
    
    if (t == NULL) {
        return;
    }

    int event_count = t->event_count;
    int first_event = (t->next_event - event_count + TRACE_EVENTS_BUFFER) % TRACE_EVENTS_BUFFER;
    int remaining_events = event_count;

    printf("core_id=%d,n_events=%d\n", core_id, event_count);
    while (remaining_events > 0) {
        printf("%016llx,", t->buffer[first_event].data);
        first_event = (first_event + 1) % TRACE_EVENTS_BUFFER;
        remaining_events -= 1;
    }
    printf("\n");
    printf("\n");

    t->event_count -= event_count;
}

void trace_buffer_dump() {
    printf("=================================\n");
    printf("BEGIN EVENT TRACE DUMP\n");

    for (int i = 0; i < TRACE_NUM_CORES; i++) {
        trace_buffer_dump_core(i);
    }

    printf("END EVENT TRACE DUMP\n");
    printf("=================================\n");
}

// Trace buffer tasks

static SemaphoreHandle_t trace_semaphore;

static void IRAM_ATTR trace_dump_callback(void *arg) {
    int should_yield = false;
    xSemaphoreGiveFromISR(trace_semaphore, &should_yield);
    portYIELD_FROM_ISR(should_yield);
}

static void trace_task_dump() {
    printf("Task Name\tStatus\tPrio\tHWM\tTask\tAffinity\n");
    char stats_buffer[1024];
    vTaskList(stats_buffer);
    printf("%s\n", stats_buffer);
}

static void trace_dump_task(void *arg) {
    // Install ISR handler for GPIO interrupts
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

    gpio_config_t trace_dump_conf = {
        .pin_bit_mask = (1ull << GPIO_TRACE_DUMP),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&trace_dump_conf);
    gpio_isr_handler_add(GPIO_TRACE_DUMP, trace_dump_callback, NULL);
    gpio_intr_enable(GPIO_TRACE_DUMP);

    while (true) {
        xSemaphoreTake(trace_semaphore, portMAX_DELAY);

        trace_sync_all();

        trace_buffer_dump();
        trace_task_dump();
    }
}

void trace_buffer_init_all() {
    for (int i = 0; i < TRACE_NUM_CORES; i++) {
        trace_buffer_init(i);
    }

    for (int i = 0; i < TRACE_NUM_CORES; i++) {
        esp_ipc_call_blocking(i, trace_buffer_start, NULL);
    }

    trace_semaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(trace_dump_task, "trace_dump_task", 4096, NULL, TRACE_DUMP_TASK_PRIORITY, NULL, TRACE_DUMP_TASK_CORE_ID);
}
