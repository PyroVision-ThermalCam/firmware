/*
 * network_types.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Common type definitions for the network interface component.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Errors and commissions should be reported to DanielKampert@kampis-elektroecke.de
 */

#ifndef NETWORK_TYPES_H_
#define NETWORK_TYPES_H_

#include <esp_err.h>
#include <esp_event.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Declare network event base */
ESP_EVENT_DECLARE_BASE(NETWORK_EVENTS);

/** @brief Network connection state.
 */
typedef enum {
    NETWORK_STATE_IDLE = 0,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_PROVISIONING,
    NETWORK_STATE_AP_STARTED,
    NETWORK_STATE_ERROR,
} Network_State_t;

/** @brief WiFi operation mode.
 */
typedef enum {
    NETWORK_WIFI_MODE_STA = 0,
    NETWORK_WIFI_MODE_AP,
    NETWORK_WIFI_MODE_APSTA,
} Network_WiFiMode_t;

/** @brief Provisioning method.
 */
typedef enum {
    NETWORK_PROV_NONE = 0,
    NETWORK_PROV_BLE,
    NETWORK_PROV_SOFTAP,
    NETWORK_PROV_BOTH,
} Network_ProvMethod_t;

/** @brief Image format for encoding.
 */
typedef enum {
    NETWORK_IMAGE_FORMAT_JPEG = 0,
    NETWORK_IMAGE_FORMAT_PNG,
    NETWORK_IMAGE_FORMAT_RAW,
} Network_ImageFormat_t;

/** @brief Network event types (used as event IDs in NETWORK_EVENTS base).
 */
typedef enum {
    NETWORK_EVENT_WIFI_CONNECTED = 0,
    NETWORK_EVENT_WIFI_DISCONNECTED,
    NETWORK_EVENT_WIFI_GOT_IP,                  /**< The device got an IP address
                                                     Data is of type Network_IP_Info_t */
    NETWORK_EVENT_AP_STARTED,
    NETWORK_EVENT_AP_STOPPED,
    NETWORK_EVENT_AP_STA_CONNECTED,
    NETWORK_EVENT_AP_STA_DISCONNECTED,
    NETWORK_EVENT_PROV_STARTED,
    NETWORK_EVENT_PROV_STOPPED,
    NETWORK_EVENT_PROV_CRED_RECV,
    NETWORK_EVENT_PROV_SUCCESS,
    NETWORK_EVENT_PROV_FAILED,
    NETWORK_EVENT_PROV_TIMEOUT,
    NETWORK_EVENT_WS_CLIENT_CONNECTED,
    NETWORK_EVENT_WS_CLIENT_DISCONNECTED,
    NETWORK_EVENT_OTA_STARTED,
    NETWORK_EVENT_OTA_PROGRESS,
    NETWORK_EVENT_OTA_COMPLETED,
    NETWORK_EVENT_OTA_FAILED,
    NETWORK_EVENT_SNTP_SYNCED,                  /**< SNTP time synchronization completed
                                                     Data is of type struct timeval */
    NETWORK_EVENT_SET_TZ,                       /**< Set the timezone
                                                     Data is a const char* string */
    NETWORK_EVENT_OPEN_WIFI_REQUEST,            /**< Request to open a WiFi connection */
    NETWORK_EVENT_SERVER_STARTED,               /**< HTTP/WebSocket server started */
    NETWORK_EVENT_SERVER_STOPPED,               /**< HTTP/WebSocket server stopped */
    NETWORK_EVENT_SERVER_ERROR,                 /**< HTTP/WebSocket server error */
} Network_Event_t;

/** @brief Color palette types.
 */
typedef enum {
    PALETTE_IRON = 0,
    PALETTE_GRAY,
    PALETTE_RAINBOW,
} Server_Palette_t;

/** @brief Scale mode for temperature visualization.
 */
typedef enum {
    SCALE_LINEAR = 0,
    SCALE_HISTOGRAM,
} Server_Scale_t;

/** @brief LED state.
 */
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_BLINK,
} Server_LED_State_t;

/** @brief WebSocket message types.
 */
typedef enum {
    WS_MSG_TYPE_EVENT = 0,
    WS_MSG_TYPE_COMMAND,
    WS_MSG_TYPE_RESPONSE,
} Server_WS_Message_Type_t;

/** @brief Thermal frame data structure.
 */
typedef struct {
    uint8_t *buffer;                    /**< Pointer to RGB888 image data */
    uint16_t width;                     /**< Frame width in pixels */
    uint16_t height;                    /**< Frame height in pixels */
    float temp_min;                     /**< Minimum temperature in frame */
    float temp_max;                     /**< Maximum temperature in frame */
    float temp_avg;                     /**< Average temperature in frame */
    uint32_t timestamp;                 /**< Timestamp in milliseconds */
    SemaphoreHandle_t mutex;            /**< Mutex for thread-safe access */
} Network_Thermal_Frame_t;

/** @brief Encoded image data.
 */
typedef struct {
    uint8_t *data;                      /**< Encoded image data */
    size_t size;                        /**< Size of encoded data */
    Network_ImageFormat_t format;       /**< Image format */
    uint16_t width;                     /**< Image width */
    uint16_t height;                    /**< Image height */
} Network_Encoded_Image_t;

/** @brief IP info event data (for NETWORK_EVENT_WIFI_GOT_IP).
 */
typedef struct {
    uint32_t IP;
    uint32_t Netmask;
    uint32_t Gateway;
} Network_IP_Info_t;

/** @brief Station info event data (for AP_STA_CONNECTED/DISCONNECTED).
 */
typedef struct {
    uint8_t MAC[6];
} Network_Event_STA_Info_t;

/** @brief OTA progress event data (for NETWORK_EVENT_OTA_PROGRESS).
 */
typedef struct {
    uint32_t bytes_written;
    uint32_t total_bytes;
} Network_Event_OTA_Progress_t;

/** @brief WebSocket client event data.
 */
typedef struct {
    int client_fd;
} Network_Event_WS_Client_t;

/** @brief WiFi station configuration.
 */
typedef struct {
    char ssid[33];
    char password[65];
    uint8_t max_retries;
    uint16_t retry_interval_ms;
} Network_WiFi_STA_Config_t;

/** @brief WiFi access point configuration.
 */
typedef struct {
    char ssid[33];
    char password[65];
    uint8_t channel;
    uint8_t max_connections;
    bool hidden;
} Network_WiFi_AP_Config_t;

/** @brief Network interface configuration.
 */
typedef struct {
    Network_WiFiMode_t WiFi_Mode;
    Network_WiFi_STA_Config_t STA_Config;
    Network_WiFi_AP_Config_t AP_Config;
    Network_ProvMethod_t Prov_Method;
} Network_Config_t;

/** @brief LED control request.
 */
typedef struct {
    Server_LED_State_t state;
    uint8_t brightness;
    uint16_t blink_ms;
} Server_LED_Request_t;

/** @brief Server configuration.
 */
typedef struct {
    uint16_t http_port;
    uint8_t max_clients;
    uint16_t ws_ping_interval_sec;
    bool enable_cors;
    const char *api_key;
} Server_Config_t;

/** @brief Server status.
 */
typedef struct {
    bool running;
    uint8_t http_clients;
    uint8_t ws_clients;
    uint32_t requests_served;
    uint32_t frames_streamed;
} Server_Status_t;

#endif /* NETWORK_TYPES_H_ */
