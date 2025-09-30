/*
 * wifi.h
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

#ifndef __CPX_WIFI_H__
#define __CPX_WIFI_H__

#include <esp_wifi.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* The SSID of the AI-deck when in AP mode */
#define WIFI_SSID "crazyflie"

/* TCP and UDP local ports */
#define PORT 5000

/* Initialize the WiFi */
void cpx_wifi_init(wifi_mode_t mode, const char *ssid, const char *key);

/* Check if Wi-Fi has been started */
bool wifi_has_started();

/* Wait (and block) until a connection comes in */
void wifi_wait_for_socket_connected();

/* Bind socket for incomming connections */
void wifi_bind_socket();

/* Check if a client is connected */
bool wifi_is_socket_connected();

/* Wait (and block) for a client to disconnect */
void wifi_wait_for_disconnect();

/* Send a packet to the client from buffer of size */
void wifi_send_packet(const uint8_t *buffer, size_t size);

/* Wait (and block) for a received packet */
void wifi_receive_packet(uint8_t **buffer);
void wifi_release_receive(uint8_t *buffer);

#endif /* __CPX_WIFI_H__ */
