/*
 * streamer.c
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
 */


#include "config.h"
#include "cpx_spi.h"
#include "cpx_wifi.h"
#include "trace_buffer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

#include <stdint.h>

static const char *TAG = "cpx";

/* Wi-Fi SSID/password and AP/station is set from menuconfig */
#ifdef CONFIG_USE_AS_AP
#define WIFI_MODE WIFI_MODE_AP
#define CONFIG_EXAMPLE_SSID NULL
#define CONFIG_EXAMPLE_PASSWORD NULL
#else
#define WIFI_MODE WIFI_MODE_STA
#endif

/* LED blinking periods [ms] */
static volatile uint32_t led_period_off = 500;
static volatile uint32_t led_period_on = 500;

static volatile int connected = 0;
static volatile int source_alive = 0;
static volatile int got_msg = 0;

/* LED Task */
static void update_led() {
    if (source_alive) {
        if (connected) {
            led_period_on = led_period_off = 100;
        } else {
            led_period_on = led_period_off = 500;
        }
    } else {
        if (connected) {
            led_period_off = 200;
            led_period_on = 1000;
        } else {
            led_period_on = led_period_off = 2000;
        }
    }
}

static void led_task(void *pvParameters) {
    while (1) {
        gpio_set_level(GPIO_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(led_period_off));
        gpio_set_level(GPIO_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(led_period_on));

        trace_sync_all();
    }
}

static void led_init() {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ull << GPIO_LED),
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_conf);
    gpio_set_level(GPIO_NINA_RTT, 0);

    update_led();

    xTaskCreatePinnedToCore(led_task, "led", 4096, NULL, LED_TASK_PRIORITY, NULL, LED_TASK_CORE_ID);
}

/* CPX Wi-Fi task */
static void wifi_status_task(void *pvParameters) {
    wifi_bind_socket();
    while (1) {
        wifi_wait_for_socket_connected();
        connected = 1;
        update_led();
        wifi_wait_for_disconnect();
        connected = 0;
        update_led();
        ESP_LOGI(TAG, "Client disconnected");
    }
}

static void wifi_init() {
    cpx_wifi_init(WIFI_MODE, CONFIG_EXAMPLE_SSID, CONFIG_EXAMPLE_PASSWORD);
    xTaskCreatePinnedToCore(wifi_status_task, "wifi_status_task", 4096, NULL, WIFI_STATUS_TASK_PRIORITY, NULL, WIFI_STATUS_TASK_CORE_ID);
}

/* CPX router tasks */
static void cpx_router_spi_task(void *pvParameters) {
    ESP_LOGI(TAG, "cpx_router_spi_task started");

    while (true) {
        uint8_t *spi_buffer = NULL;
        cpx_spi_receive_packet(&spi_buffer);

        cpx_spi_header_t *spi_header = (cpx_spi_header_t *)spi_buffer;
        if (spi_header->length > 0) {
            got_msg = 1;

            uint16_t spi_length = sizeof(cpx_spi_header_t) + spi_header->length;

            if (wifi_is_socket_connected()) {
                ESP_LOGD(TAG, "Sending Wi-Fi packet %p with length %d", spi_buffer, spi_length);
                wifi_send_packet(spi_buffer, spi_length);
            }
        }

        cpx_spi_release_receive(spi_buffer);
        spi_buffer = NULL;
    }
}

static void cpx_router_wifi_task(void *pvParameters) {
    ESP_LOGI(TAG, "cpx_router_wifi_task started");

    while (1) {
        uint8_t *wifi_buffer = NULL;
        wifi_receive_packet(&wifi_buffer);
        cpx_spi_send_packet(wifi_buffer);

        ESP_LOGD(TAG, "Received Wi-Fi buffer %p", wifi_buffer);

        uint8_t *tx_done = NULL;
        cpx_spi_send_wait_done(&tx_done);

        if (tx_done != wifi_buffer) {
            ESP_LOGE(TAG, "tx_done buffer %p does not match expected wifi_buffer %p", tx_done, wifi_buffer);
        }

        wifi_release_receive(wifi_buffer);
        wifi_buffer = NULL;
    }
}

static void cpx_router_init() {
    xTaskCreatePinnedToCore(cpx_router_spi_task,  "cpx_router_spi_task",  4096, NULL, CPX_ROUTER_TASK_PRIORITY, NULL, CPX_ROUTER_TASK_CORE_ID);
    xTaskCreatePinnedToCore(cpx_router_wifi_task, "cpx_router_wifi_task", 4096, NULL, CPX_ROUTER_TASK_PRIORITY, NULL, CPX_ROUTER_TASK_CORE_ID);
}

/* Main task */
void app_main() {
    // NOTE: to enable this, CONFIG_LOG_MAXIMUM_LEVEL must also be set to DEBUG in menuconfig
    // esp_log_level_set("cpx", ESP_LOG_DEBUG);
    // esp_log_level_set("cpx_wifi", ESP_LOG_DEBUG);
    // esp_log_level_set("cpx_spi", ESP_LOG_DEBUG);

    int wakeup_cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "Wake up cause: %d", wakeup_cause);

    // Initialize event trace functionality
    // NOTE: also sets up GPIO interrupts, which are used by CPX SPI too.
    // Since a single shared GPIO ISR is registered on the core that calls 
    // gpio_install_isr_service, trace_dump_task and cpx_spi_transfer_task 
    // should 1) be pinned to a specific core, and 2) both should share the
    // same core.
    trace_buffer_init_all();

    wifi_init();
    cpx_spi_init();
    cpx_router_init();
    led_init();

    while (1) {
        got_msg = 0;
        vTaskDelay(pdMS_TO_TICKS(2000));

        source_alive = got_msg;
        update_led();
    }
}
