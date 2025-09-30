/*
 * cpx_spi.h
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

#ifndef __CPX_SPI_H__
#define __CPX_SPI_H__

#include "cpx_types.h"

#include <driver/spi_slave.h>

// CPX SPI header must be >= 4 bytes long and aligned to 4 bytes to account for GAP SPI limitations
typedef struct cpx_spi_header_s {
    // Length of the cpx_spi payload (maximum supported size is CPX_SPI_MTU)
    uint16_t length;

    // CPX header with routing information
    cpx_header_t cpx;
} __attribute__((packed)) cpx_spi_header_t;

// Maximum total size of a CPX SPI packet (header + payload)
// Currently: maximum supported DMA transfer length by ESP32 (SPI_MAX_DMA_LEN = 4092 bytes)
#define CPX_SPI_MAX_PACKET_LENGTH (SPI_MAX_DMA_LEN)

// Maximum payload size of a CPX SPI packet
#define CPX_SPI_MTU (CPX_SPI_MAX_PACKET_LENGTH - sizeof(cpx_spi_header_t))

/* Initialize the SPI */
void cpx_spi_init();

void cpx_spi_receive_packet(uint8_t **buffer);
void cpx_spi_release_receive(uint8_t *buffer);

void cpx_spi_send_packet(uint8_t *buffer);
void cpx_spi_send_wait_done(uint8_t **buffer);

#endif /* __CPX_SPI_H__ */
