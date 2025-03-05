/*
 * wifi.c
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

#include "cpx_wifi.h"

#include "config.h"
#include "cpx_spi.h"
#include "trace_buffer.h"
#include "utils.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <nvs_flash.h>

#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <string.h>

#define WIFI_RX_QUEUE_LENGTH 3

const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_SOCKET_CONNECTED = BIT1;
const int WIFI_SOCKET_DISCONNECTED = BIT2;
static EventGroupHandle_t wifi_event_group;

/* Log printout tag */
static const char *TAG = "cpx_wifi";

/* Wi-Fi has started */
static bool started = false;
/* Socket for receiving Wi-Fi connections */
static int sock = -1;
/* Accepted WiFi connection */
static int conn = -1;

static QueueHandle_t free_queue;
static QueueHandle_t rx_queue;

/* UDP transport */
static int udp_sock = -1;
static uint16_t next_tx_seq = -1;
static uint16_t next_rx_seq = -1;

int esp_wifi_internal_set_retry_counter(int src, int lrc);

/* Wi-Fi event handler */
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    // WIFI_MODE_AP
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
        }
    }
    // WIFI_MODE_STA
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "Disconnected from access point, reconnecting");
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

/* Initialize WiFi as AP */
static void wifi_init_ap() {
    const char *ssid = WIFI_SSID;

    ESP_LOGI(TAG, "Access point mode (ssid: %s)", ssid);

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 1,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    strlcpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    started = true;
}

/* Initialize Wi-Fi as station (connecting to AP) */
static void wifi_init_sta(const char *ssid, const char *passwd) {
    ESP_LOGI(TAG, "Station mode (ssid: %s, password: %s)", ssid, passwd);
    
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, passwd, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Prevent ESP32 from sleeping and causing high ping and socket accept times
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    started = true;
}

bool wifi_has_started() {
    return started;
}

static void wifi_tcp_send_packet();

static void wifi_udp_bind_socket(struct sockaddr_in *remoteAddr, socklen_t addrLen);
static void wifi_udp_disconnect_socket();
static void wifi_udp_send_packet();

void wifi_bind_socket() {
    int err;
    
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: error %s (%d)", strerror(errno), errno);
    }
    ESP_LOGI(TAG, "Socket created");

    struct sockaddr_in localAddr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = htons(PORT)
    };
    err = bind(sock, (struct sockaddr *)&localAddr, sizeof(localAddr));
    if (err) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %s (%d)", strerror(errno), errno);
    }
    ESP_LOGI(TAG, "Socket bound");

    err = listen(sock, 1);
    if (err) {
        ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
    }
    ESP_LOGI(TAG, "Socket listening");
}

void wifi_wait_for_socket_connected() {
    ESP_LOGI(TAG, "Waiting for connection");
    struct sockaddr_in remoteAddr = {0};
    socklen_t addrLen = sizeof(remoteAddr);
    conn = accept(sock, (struct sockaddr *)&remoteAddr, &addrLen);
    if (conn < 0) {
        ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
    }
    ESP_LOGI(TAG, "Connection accepted");

    wifi_udp_bind_socket(&remoteAddr, addrLen);

    xEventGroupClearBits(wifi_event_group, WIFI_SOCKET_DISCONNECTED);
    xEventGroupSetBits(wifi_event_group, WIFI_SOCKET_CONNECTED);

    trace_event(TRACE_EVT_CPX_TCP_CONNECTION, TRACE_BEGIN, 0);
}

bool wifi_is_socket_connected() {
    return conn != -1;
}

static void wifi_handle_socket_error() {
    close(conn);
    conn = -1;

    wifi_udp_disconnect_socket();

    xEventGroupClearBits(wifi_event_group, WIFI_SOCKET_CONNECTED);
    xEventGroupSetBits(wifi_event_group, WIFI_SOCKET_DISCONNECTED);

    trace_event(TRACE_EVT_CPX_TCP_CONNECTION, TRACE_END, 0);
}

void wifi_wait_for_disconnect() {
    xEventGroupWaitBits(wifi_event_group, WIFI_SOCKET_DISCONNECTED, pdTRUE, pdFALSE, portMAX_DELAY);
}

void wifi_send_packet(const uint8_t *buffer, size_t size) {
#if CONFIG_ENABLE_UDP_TX
    wifi_udp_send_packet(buffer, size);
#else
    wifi_tcp_send_packet(buffer, size);
#endif
}

// MARK: TCP transport

typedef struct cpx_tcp_header_s {
    // Length of the cpx_spi payload (maximum supported size is CPX_SPI_MTU)
    uint16_t length;

    // CPX header with routing information
    cpx_header_t cpx;
} __attribute__((packed)) cpx_tcp_header_t;

#define CPX_TCP_MAX_PACKET_LENGTH (CPX_SPI_MAX_PACKET_LENGTH)
#define CPX_TCP_MTU (CPX_TCP_MAX_PACKET_LENGTH - sizeof(cpx_tcp_header_t))

__attribute__((unused))
void wifi_tcp_send_packet(const uint8_t *buffer, size_t size) {
    if (conn == -1) {
        ESP_LOGE(TAG, "No socket when trying to send data");
        return;
    }

    cpx_spi_header_t spi_header = *(cpx_spi_header_t *)(buffer);

    // Rewrite the packet header to a CPX TCP header
    // TODO: this works only because the TCP and SPI headers have the same size
    cpx_tcp_header_t *header = (cpx_tcp_header_t *)(buffer);
    *header = (cpx_tcp_header_t){
        .length = spi_header.length,
        .cpx = spi_header.cpx};

    trace_event(TRACE_EVT_CPX_TCP_SEND, TRACE_BEGIN, (uint16_t)(uintptr_t)buffer);
    int err = send(conn, buffer, size, 0);
    trace_event(TRACE_EVT_CPX_TCP_SEND, TRACE_END, err);

    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: error %s (%d)", strerror(errno), errno);
        wifi_handle_socket_error();
    }
}

static void cpx_tcp_rx_task(void *pvParameters) {
    while (1) {
        if (conn == -1) {
            xEventGroupWaitBits(wifi_event_group, WIFI_SOCKET_CONNECTED, pdTRUE, pdFALSE, portMAX_DELAY);
        }

        uint8_t *rx_buffer;
        xQueueReceive(free_queue, &rx_buffer, portMAX_DELAY);

        ESP_LOGD(TAG, "Has Wi-Fi rx buffer %p", rx_buffer);
        // memset(rx_buffer, 0x99, CPX_SPI_MAX_PACKET_LENGTH);

        trace_event(TRACE_EVT_CPX_TCP_RECEIVE, TRACE_BEGIN, (uint16_t)(uintptr_t)rx_buffer);

        ESP_LOGD(TAG, "Starting recv");
        ssize_t len = recv(conn, rx_buffer, sizeof(cpx_tcp_header_t), 0);
        if (len <= 0) {
            trace_event(TRACE_EVT_CPX_TCP_RECEIVE, TRACE_END, errno);

            ESP_LOGE(TAG, "Error occurred during receive: len %d, error %s (%d)", len, strerror(errno), errno);
            wifi_release_receive(rx_buffer);
            wifi_handle_socket_error();
            continue;
        }

        cpx_tcp_header_t header = *(cpx_tcp_header_t *)(rx_buffer);
        ESP_LOGD(TAG, "Recv packet header: length %d", header.length);

        uint8_t *payload = rx_buffer + sizeof(cpx_tcp_header_t);
        size_t payload_length = MIN(header.length, CPX_SPI_MTU);

        size_t received_length = 0;
        while (received_length < payload_length) {
            len = recv(conn, payload + received_length, payload_length - received_length, 0);
            if (len > 0) {
                received_length += len;
            } else {
                break;
            }
        }

        if (received_length != payload_length) {
            trace_event(TRACE_EVT_CPX_TCP_RECEIVE, TRACE_END, errno);

            ESP_LOGE(TAG, "Error occurred during receive: len %d, error %s (%d), received_length: %d", len, strerror(errno), errno, received_length);
            wifi_release_receive(rx_buffer);
            wifi_handle_socket_error();
            continue;
        }

        trace_event(TRACE_EVT_CPX_TCP_RECEIVE, TRACE_END, 0);

        // Rewrite the packet header to a CPX SPI header
        // TODO: this works only because the TCP and SPI headers have the same size
        cpx_spi_header_t *spi_header = (cpx_spi_header_t *)(rx_buffer);
        *spi_header = (cpx_spi_header_t){
            .length = received_length,
            .cpx = header.cpx};

        xQueueSend(rx_queue, &rx_buffer, portMAX_DELAY);
    }
}

void wifi_receive_packet(uint8_t **buffer) {
    xQueueReceive(rx_queue, buffer, portMAX_DELAY);
}

void wifi_release_receive(uint8_t *buffer) {
    xQueueSend(free_queue, &buffer, portMAX_DELAY);
}

static void wifi_init_mdns() {
    // Enable mDNS so that NINA is reacheable as hostname.local
    // Initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGE(TAG, "mDNS Init failed: %s", esp_err_to_name(err));
        return;
    }

    // Set hostname
    const char *hostname = CONFIG_MDNS_HOSTNAME;
    mdns_hostname_set(hostname);
    ESP_LOGI(TAG, "mDNS hostname set to: [%s]", hostname);

    // Set human-readable default instance name
    mdns_instance_name_set("IDSIA AI-deck CPX streamer");

    // Add service
    mdns_service_add(NULL, "_cpx", "_tcp", 5000, NULL, 0);
}

// MARK: UDP transport

typedef struct cpx_udp_header_s
{
    // Sequence number to detect out-of-order packets
    uint16_t sequence;

    // CPX header with routing information
    cpx_header_t cpx;
} __attribute__((packed)) cpx_udp_header_t;

#define CPX_UDP_MAX_PACKET_LENGTH (CPX_SPI_MAX_PACKET_LENGTH)
#define CPX_UDP_MTU (CPX_UDP_MAX_PACKET_LENGTH - sizeof(cpx_udp_header_t))

static void wifi_udp_bind_socket(struct sockaddr_in *remoteAddr, socklen_t addrLen) {
    int err;
    char addr_str[128];

    udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_sock < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: error %s (%d)", strerror(errno), errno);
    }
    ESP_LOGI(TAG, "UDP socket created");

    struct sockaddr_in localAddr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = htons(PORT)
    };
    err = bind(udp_sock, (struct sockaddr *)&localAddr, sizeof(localAddr));
    if (err) {
        ESP_LOGE(TAG, "Unable to bind UDP socket: errno %s (%d)", strerror(errno), errno);
    }
    inet_ntoa_r(localAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
    ESP_LOGI(TAG, "UDP socket bound to local address %s, port %d", addr_str, ntohs(localAddr.sin_port));

    err = connect(udp_sock, (struct sockaddr *)remoteAddr, addrLen);
    if (err) {
        ESP_LOGE(TAG, "Unable to connect UDP socket: errno %s (%d)", strerror(errno), errno);
    }
    inet_ntoa_r(remoteAddr->sin_addr, addr_str, sizeof(addr_str) - 1);
    ESP_LOGI(TAG, "UDP socket connected to remote address %s, port %d", addr_str, ntohs(remoteAddr->sin_port));

    next_tx_seq = 0;
    next_rx_seq = 0;
}

static void wifi_handle_udp_socket_error() {
    wifi_udp_disconnect_socket();
}

static void wifi_udp_disconnect_socket() {
    close(udp_sock);
    udp_sock = -1;
    next_tx_seq = -1;
    next_rx_seq = -1;
}

__attribute__((unused)) void wifi_udp_send_packet(const uint8_t *buffer, size_t size) {
    if (!wifi_is_socket_connected()) {
        ESP_LOGE(TAG, "No connection");
        return;
    }

    if (udp_sock == -1) {
        ESP_LOGE(TAG, "No socket when trying to send data");
        return;
    }

    cpx_spi_header_t spi_header = *(cpx_spi_header_t *)(buffer);

    // Rewrite the packet header to a CPX UDP header
    // TODO: this works only because the UDP and SPI headers have the same size
    cpx_udp_header_t *header = (cpx_udp_header_t *)(buffer);
    *header = (cpx_udp_header_t){
        .sequence = next_tx_seq,
        .cpx = spi_header.cpx};
    next_tx_seq += 1;

    trace_event(TRACE_EVT_CPX_UDP_SEND, TRACE_BEGIN, (uint16_t)(uintptr_t)buffer);
    int err = send(udp_sock, buffer, size, 0);
    trace_event(TRACE_EVT_CPX_UDP_SEND, TRACE_END, err);

    if (err >= 0) {
        return;
    }

    if (errno == ENOMEM) {
        ESP_LOGD(TAG, "UDP send packet dropped: error %s (%d)", strerror(errno), errno);
        return;
    } else {
        ESP_LOGE(TAG, "Error occurred during UDP send: error %s (%d)", strerror(errno), errno);
        wifi_handle_socket_error();
    }
}

static void cpx_udp_rx_task(void *pvParameters) {
    while (1) {
        if (conn == -1) {
            xEventGroupWaitBits(wifi_event_group, WIFI_SOCKET_CONNECTED, pdTRUE, pdFALSE, portMAX_DELAY);
        }

        uint8_t *rx_buffer;
        xQueueReceive(free_queue, &rx_buffer, portMAX_DELAY);

        ESP_LOGD(TAG, "Has Wi-Fi rx buffer %p", rx_buffer);
        // memset(rx_buffer, 0x99, CPX_UDP_MAX_PACKET_LENGTH);

        trace_event(TRACE_EVT_CPX_UDP_RECEIVE, TRACE_BEGIN, (uint16_t)(uintptr_t)rx_buffer);
        ssize_t length = recv(udp_sock, rx_buffer, CPX_UDP_MAX_PACKET_LENGTH, MSG_TRUNC);
        trace_event(TRACE_EVT_CPX_UDP_RECEIVE, TRACE_END, errno);

        if (length < sizeof(cpx_udp_header_t)) {
            ESP_LOGE(TAG, "Error occurred during UDP receive: len %d, error %s (%d)", length, strerror(errno), errno);
            wifi_release_receive(rx_buffer);
            wifi_handle_udp_socket_error();
            continue;
        }

        if (length > CPX_UDP_MAX_PACKET_LENGTH) {
            ESP_LOGE(TAG, "UDP packet exceeds maximum length %d", length);
            wifi_release_receive(rx_buffer);
            wifi_handle_udp_socket_error();
            continue;
        }

        cpx_udp_header_t header = *(cpx_udp_header_t *)(rx_buffer);
        size_t received_length = length - sizeof(cpx_udp_header_t);

        if (header.sequence < next_rx_seq) {
            ESP_LOGW(TAG, "UDP packet received with sequence number %d, expected (%d). Discarding", header.sequence, next_rx_seq);
            next_rx_seq = 0;
            wifi_release_receive(rx_buffer);
            continue;
        }

        next_rx_seq = header.sequence + 1;

        // Rewrite the packet header to a CPX SPI header
        // TODO: this works only because the UDP and SPI headers have the same size
        cpx_spi_header_t *spi_header = (cpx_spi_header_t *)(rx_buffer);
        *spi_header = (cpx_spi_header_t){
            .length = received_length,
            .cpx = header.cpx};

        xQueueSend(rx_queue, &rx_buffer, portMAX_DELAY);
    }
}

void cpx_wifi_init(wifi_mode_t mode, const char *ssid, const char *key) {
    ESP_LOGD(TAG, "Debug log enabled");

    if (CPX_SPI_MAX_PACKET_LENGTH != CPX_TCP_MAX_PACKET_LENGTH || CPX_SPI_MAX_PACKET_LENGTH != CPX_UDP_MAX_PACKET_LENGTH) {
        ESP_LOGE(
            TAG, "Max packet lengths must match between SPI (%d), TCP (%d) and UDP (%d) transports in current implementation",
            CPX_SPI_MAX_PACKET_LENGTH, CPX_TCP_MAX_PACKET_LENGTH, CPX_UDP_MAX_PACKET_LENGTH);
        exit(1);
    }

    if (sizeof(cpx_spi_header_t) != sizeof(cpx_tcp_header_t) || sizeof(cpx_spi_header_t) != sizeof(cpx_udp_header_t)) {
        ESP_LOGE(
            TAG, "Header lengths must match between SPI (%d), TCP (%d) and UDP (%d) transports in current implementation",
            sizeof(cpx_spi_header_t), sizeof(cpx_tcp_header_t), sizeof(cpx_udp_header_t));
        exit(1);
    }

    // Initialize NVS Flash memory to store Wi-Fi calibration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (WIFI_MODE_AP == mode) {
        wifi_init_ap();
    } else {
        wifi_init_sta(ssid, key);
    }

    // esp_wifi_internal_set_retry_counter(31, 0);

#if CONFIG_ENABLE_MDNS
    wifi_init_mdns();
#endif

    free_queue = xQueueCreate(WIFI_RX_QUEUE_LENGTH, sizeof(uint8_t *));
    rx_queue = xQueueCreate(WIFI_RX_QUEUE_LENGTH, sizeof(uint8_t *));

    for (int i = 0; i < WIFI_RX_QUEUE_LENGTH; i++) {
        // TODO: this works only because SPI, TCP, and UDP all have the same max packet length
        uint8_t *rx_buffer = (uint8_t *)heap_caps_malloc(CPX_SPI_MAX_PACKET_LENGTH, MALLOC_CAP_DMA);
        xQueueSend(free_queue, &rx_buffer, portMAX_DELAY);
        ESP_LOGI(TAG, "Wi-Fi rx buffer allocated: %d bytes @ %p", CPX_SPI_MAX_PACKET_LENGTH, rx_buffer);
    }

    xTaskCreatePinnedToCore(cpx_tcp_rx_task, "Wi-Fi TCP RX", 5000, NULL, CPX_TCP_TASK_PRIORITY, NULL, CPX_TCP_TASK_CORE_ID);
    xTaskCreatePinnedToCore(cpx_udp_rx_task, "Wi-Fi UDP RX", 5000, NULL, CPX_UDP_TASK_PRIORITY, NULL, CPX_UDP_TASK_CORE_ID);

    ESP_LOGI(TAG, "Wi-Fi initialized");
}
