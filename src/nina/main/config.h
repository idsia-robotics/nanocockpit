/*
 * config.h
 * Elia Cereda <elia.cereda@idsia.ch>
 *
 * Copyright (C) 2025 IDSIA, USI-SUPSI
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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <esp_task.h>

/**************************** TASK CONFIGURATION ****************************/
// NOTE: due to ESP-IDF limitation, tasks pinned to different cores should not share the same priority

#define LED_TASK_CORE_ID            (0)
#define LED_TASK_PRIORITY           (2)

#define WIFI_STATUS_TASK_CORE_ID    (1)
#define WIFI_STATUS_TASK_PRIORITY   (3)

#define CPX_ROUTER_TASK_CORE_ID     (1)
#define CPX_ROUTER_TASK_PRIORITY    (3)

#define CPX_TCP_TASK_CORE_ID        (1)
#define CPX_TCP_TASK_PRIORITY       (4)

#define CPX_UDP_TASK_CORE_ID        (1)
#define CPX_UDP_TASK_PRIORITY       (4)

#define CPX_SPI_TASK_CORE_ID        (1)
#define CPX_SPI_TASK_PRIORITY       (ESP_TASK_TCPIP_PRIO+1)

#define TRACE_DUMP_TASK_CORE_ID     (1)
#define TRACE_DUMP_TASK_PRIORITY    (24)

/******************************* GPIO SETTINGS ******************************/
//                      ESP32 GPIO              NINA-W10 Pin Names
#define GPIO_LED             (4)        //      Pin 24 / GPIO_24 / RMII_MDIO
#define GPIO_TRACE_DUMP      (0)        //      Pin 27 / RMII_CLK / SYS_BOOT

// NOTE: MISO and MOSI pins seem swapped compared to ESP32 documentation. If this
// is indeed the case, this routes all SPI signals through the GPIO matrix, increasing
// the MISO input delay and limiting the achievable SPI clock frequency to 7.2MHz
// (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/spi_slave.html#gpio-matrix-and-io-mux)
#define GPIO_MOSI           (19)        //      Pin 21 / GPIO_21 / SPI_V_MISO
#define GPIO_MISO           (23)        //      Pin  1 / GPIO_1  / SPI_V_MOSI
#define GPIO_SCLK           (18)        //      Pin 29 / GPIO_29 / SPI_V_CLK
#define GPIO_CS              (5)        //      Pin 28 / GPIO_28 / SPI_V_CS
#define GPIO_NINA_RTT        (2)        //      Pin 25 / RMII_MDCLK
#define GPIO_GAP_RTT        (32)        //      Pin  5 / GPIO_5

#endif /* __CONFIG_H__ */
