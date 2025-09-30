/*
 * cpx_spi.c
 * Elia Cereda <elia.cereda@idsia.ch>
 * Jérôme Guzzi <jerome@idsia.ch>
 * Esteban Gougeon <esteban.gougeon@greenwaves-technologies.com>
 * Germain Haugou <germain.haugou@greenwaves-technologies.com>
 *
 * Copyright (C) 2022-2025 IDSIA, USI-SUPSI
 * Copyright (C) 2020 Bitcraze AB
 * Copyright (C) 2019 GreenWaves Technologies
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

#include "cpx_spi.h"

#include "config.h"
#include "trace_buffer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/spi_slave.h>

static const char *TAG = "cpx_spi";

#define SPI_TX_QUEUE_LENGTH 1
#define SPI_RX_QUEUE_LENGTH 3

const int SPI_EVENT_GAP_RTT = BIT0;
const int SPI_EVENT_SEND = BIT1;
static EventGroupHandle_t events;

// TX queues
static QueueHandle_t tx_queue;
static QueueHandle_t tx_done_queue;

// RX queues
static QueueHandle_t free_queue;
static QueueHandle_t rx_queue;

// NINA sends random data when SPI tx_buffer is NULL, instead of zeros or anything deterministic.
// Keep an empty header, corresponding to nothing-to-send, for this case. Undefined data after the
// header will also be sent to GAP (up to CPX_MTU bytes), but will be ignored.
static cpx_spi_header_t empty_header = {0};

void cpx_spi_receive_packet(uint8_t **buffer) {
    xQueueReceive(rx_queue, buffer, portMAX_DELAY);
}

void cpx_spi_release_receive(uint8_t *buffer) {
    xQueueSend(free_queue, &buffer, portMAX_DELAY);
}

void cpx_spi_send_packet(uint8_t *buffer) {
    xQueueSend(tx_queue, &buffer, portMAX_DELAY);
    xEventGroupSetBits(events, SPI_EVENT_SEND);
}

void cpx_spi_send_wait_done(uint8_t **buffer) {
    xQueueReceive(tx_done_queue, buffer, portMAX_DELAY);
}

static void cpx_spi_transfer_task(void *pvParameters) {
    ESP_LOGI(TAG, "cpx_spi_transfer_task started");

    while (1) {
        // FIXME: fix CPX_SPI_BIDIRECTIONAL on GAP9 and re-enable
        // Wait until either NINA or GAP want to transmit
        // trace_event(TRACE_EVT_CPX_SPI_IDLE, TRACE_BEGIN, 0);
        // xEventGroupWaitBits(events, SPI_EVENT_GAP_RTT | SPI_EVENT_SEND, true, false, portMAX_DELAY);
        // trace_event(TRACE_EVT_CPX_SPI_IDLE, TRACE_END, 0);

        uint8_t *rx_buffer;
        xQueueReceive(free_queue, &rx_buffer, portMAX_DELAY); // FIXME: move packet dropping here, so that GAP is never stalled
        ESP_LOGD(TAG, "Has SPI rx buffer %p", rx_buffer);

        uint8_t *tx_buffer;
        bool has_tx = xQueueReceive(tx_queue, &tx_buffer, 0);
        ESP_LOGD(TAG, "Has SPI tx buffer %d, %p", has_tx, tx_buffer);

        spi_slave_transaction_t t = {0};
        t.length = CPX_SPI_MAX_PACKET_LENGTH * 8; // [bits]
        t.tx_buffer = has_tx ? tx_buffer : (uint8_t *)&empty_header;
        t.rx_buffer = rx_buffer;
        t.trans_len = 0;

        ESP_LOGD(TAG, "Setting up SPI slave transaction: tx_buffer %p, rx_buffer: %p", t.tx_buffer, t.rx_buffer);
        if (spi_slave_transmit(VSPI_HOST, &t, portMAX_DELAY)) {
            ESP_LOGE(TAG, "spi_slave_transmit failed");
            cpx_spi_release_receive(rx_buffer);
            continue;
        }

        size_t transfer_length = t.trans_len / 8;

        ESP_LOGD(TAG, "SPI transfer completed with length %d bytes", transfer_length);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, transfer_length, ESP_LOG_DEBUG);

        if (has_tx) {
            xQueueSend(tx_done_queue, &tx_buffer, portMAX_DELAY);
        }

        cpx_spi_header_t *rx_header = (cpx_spi_header_t *)rx_buffer;
        uint16_t rx_length = sizeof(cpx_spi_header_t) + rx_header->length;

        if (rx_length > transfer_length) {
            ESP_LOGE(TAG, "Received corrupted SPI packet with length %d while SPI transfer length was %d, discarding", rx_length, transfer_length);
            cpx_spi_release_receive(rx_buffer);
        } else {
            xQueueSend(rx_queue, &rx_buffer, portMAX_DELAY);
        }
    }
}

static void IRAM_ATTR gap_rtt_callback(void *arg) {
    int should_yield = false;

    if (gpio_get_level(GPIO_GAP_RTT) == 1) {
        // trace_event_from_isr(TRACE_EVT_CPX_SPI_GAP_RTT, TRACE_BEGIN, 0);
        xEventGroupSetBitsFromISR(events, SPI_EVENT_GAP_RTT, &should_yield);
    } else {
        // trace_event_from_isr(TRACE_EVT_CPX_SPI_GAP_RTT, TRACE_END, 0);
        xEventGroupClearBitsFromISR(events, SPI_EVENT_GAP_RTT);
    }

    portYIELD_FROM_ISR(should_yield);
}

static void IRAM_ATTR spi_post_setup_callback(spi_slave_transaction_t *t) {
    trace_event(TRACE_EVT_CPX_SPI_TRANSFER, TRACE_BEGIN, (uint16_t)(uintptr_t)t->rx_buffer);

    gpio_set_level(GPIO_NINA_RTT, 1);

    // Clear event to ensure that each GPIO_GAP_RTT positive edge is counted only
    // once. Without this, it might happen that very short SPI transfers end
    // before the negative edge on GPIO_GAP_RTT is detected. In the next iteration,
    // NINA would think that GAP already has something else to transfer, and in
    // turn set GPIO_NINA_RTT. This makes GAP think that _NINA_ has something to
    // transfer, entering a loop of spurious empty transfers that are only broken
    // when one of the two actually has data to transmit.
    xEventGroupClearBitsFromISR(events, SPI_EVENT_GAP_RTT);
}

static void IRAM_ATTR spi_post_trans_callback(spi_slave_transaction_t *t) {
    gpio_set_level(GPIO_NINA_RTT, 0);

    trace_event(TRACE_EVT_CPX_SPI_TRANSFER, TRACE_END, (uint16_t)(uintptr_t)t->tx_buffer);
}

void cpx_spi_init() {
    ESP_LOGD(TAG, "Debug log enabled");

    events = xEventGroupCreate();

    // Configure GAP ready-to-transmit input GPIO
    gpio_config_t gap_rtt_conf = {
        .pin_bit_mask = (1ull << GPIO_GAP_RTT),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&gap_rtt_conf);
    gpio_isr_handler_add(GPIO_GAP_RTT, gap_rtt_callback, NULL);
    gpio_intr_enable(GPIO_GAP_RTT);

    if (gpio_get_level(GPIO_GAP_RTT) == 1) {
        // GAP is already ready to transmit, immediately give because we will not get an interrupt
        xEventGroupSetBits(events, SPI_EVENT_GAP_RTT);
    }

    // Configure NINA ready-to-transmit output GPIO
    gpio_config_t nina_rtt_conf = {
        .pin_bit_mask = (1ull << GPIO_NINA_RTT),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&nina_rtt_conf);
    gpio_set_level(GPIO_NINA_RTT, 0);

    gpio_set_pull_mode(GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_CS, GPIO_PULLUP_ONLY);

    // Configuration for the SPI bus
    spi_bus_config_t spi_bus = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,

        // Allocate DMA structs to handle transfers up to this size
        .max_transfer_sz = CPX_SPI_MAX_PACKET_LENGTH,
    };

    // Configuration for the SPI slave interface
    spi_slave_interface_config_t spi_slave = {
        .mode = 0, // FIXME: review known issues (but mode 1-3 don't seem to work) https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_slave.html#restrictions-and-known-issues
        .spics_io_num = GPIO_CS,
        .queue_size = 3,
        .flags = 0,
        .post_setup_cb = spi_post_setup_callback,
        .post_trans_cb = spi_post_trans_callback
    };

    // Initialize SPI slave interface
    ESP_ERROR_CHECK(spi_slave_initialize(VSPI_HOST, &spi_bus, &spi_slave, 1));

    tx_queue = xQueueCreate(SPI_TX_QUEUE_LENGTH, sizeof(uint8_t *));
    tx_done_queue = xQueueCreate(SPI_TX_QUEUE_LENGTH, sizeof(uint8_t *));

    free_queue = xQueueCreate(SPI_RX_QUEUE_LENGTH, sizeof(uint8_t *));
    rx_queue = xQueueCreate(SPI_RX_QUEUE_LENGTH, sizeof(uint8_t *));

    for (int i = 0; i < SPI_RX_QUEUE_LENGTH; i++) {
        uint8_t *rx_buffer = (uint8_t *)heap_caps_malloc(CPX_SPI_MAX_PACKET_LENGTH, MALLOC_CAP_DMA);
        xQueueSend(free_queue, &rx_buffer, portMAX_DELAY);
        ESP_LOGI(TAG, "SPI rx buffer allocated: %d bytes @ %p", CPX_SPI_MAX_PACKET_LENGTH, rx_buffer);
    }

    xTaskCreatePinnedToCore(cpx_spi_transfer_task, "SPI TX/RX", 5000, NULL, CPX_SPI_TASK_PRIORITY, NULL, CPX_SPI_TASK_CORE_ID);
    ESP_LOGI(TAG, "SPI initialized");
}
