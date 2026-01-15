/*
 * provisioning.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: WiFi provisioning implementation using ESP-IDF unified provisioning.
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

#include <esp_log.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_netif.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include <string.h>

#include "../networkManager.h"
#include "../Server/server.h"
#include "../DNS/dnsServer.h"
#include "provisioning.h"

typedef struct {
    bool isInitialized;
    bool active;
    bool use_captive_portal;
    char device_name[32]; 
    char pop[32];
    uint32_t timeout_sec;
    TimerHandle_t timeout_timer;
    TaskHandle_t network_task_handle;
    wifi_sta_config_t WiFi_STA_Config;
    bool hasCredentials;
} Provisioning_State_t;

static Provisioning_State_t _Provisioning_State;

static const char *TAG = "Provisioning";

/** @brief        Timeout timer callback.
 *  @param timer  Timer handle
 */
static void on_Timeout_Timer_Handler(TimerHandle_t p_Timer)
{
    if (_Provisioning_State.network_task_handle != NULL) {
        xTaskNotify(_Provisioning_State.network_task_handle, 0x01, eSetBits);
    }
}

/** @brief              Provisioning event handler.
 *  @param p_Arg        User argument
 *  @param event_base   Event base
 *  @param event_id     Event ID
 *  @param event_data   Event data
 */
static void on_Prov_Event(void *p_Arg, esp_event_base_t EventBase, int32_t EventId, void *p_EventData)
{
    switch (EventId) {
        case WIFI_PROV_START: {
            ESP_LOGD(TAG, "Provisioning started");
            break;
        }
        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_cfg = (wifi_sta_config_t *)p_EventData;

            if (wifi_cfg != NULL) {
                memcpy(&_Provisioning_State.WiFi_STA_Config.ssid, wifi_cfg->ssid, sizeof(wifi_cfg->ssid));
                memcpy(&_Provisioning_State.WiFi_STA_Config.password, wifi_cfg->password, sizeof(wifi_cfg->password));

                _Provisioning_State.hasCredentials = true;

                ESP_LOGD(TAG, "Received WiFi credentials - SSID: %s", _Provisioning_State.WiFi_STA_Config.ssid);
            } else {
                ESP_LOGE(TAG, "WiFi config in CRED_RECV is NULL!");
            }

            break;
        }
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)p_EventData;

            ESP_LOGE(TAG, "Provisioning failed! Reason: %s", (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Auth Error" : "AP Not Found");

            break;
        }
        case WIFI_PROV_CRED_SUCCESS: {
            Network_WiFi_Credentials_t Credentials;

            ESP_LOGD(TAG, "Provisioning successful");

            if (_Provisioning_State.timeout_timer != NULL) {
                xTimerStop(_Provisioning_State.timeout_timer, 0);
            }

            memset(&Credentials, 0, sizeof(Credentials));

            /* Copy SSID and password from saved configuration */
            if (_Provisioning_State.hasCredentials) {
                strncpy(Credentials.SSID, (const char *)_Provisioning_State.WiFi_STA_Config.ssid, sizeof(Credentials.SSID) - 1);
                strncpy(Credentials.Password, (const char *)_Provisioning_State.WiFi_STA_Config.password,
                        sizeof(Credentials.Password) - 1);
            } else {
                ESP_LOGE(TAG, "No WiFi credentials available!");
            }

            NetworkManager_SetCredentials(&Credentials);

            break;
        }
        case WIFI_PROV_END: {
            ESP_LOGD(TAG, "Provisioning ended");

            wifi_prov_mgr_deinit();

            _Provisioning_State.active = false;

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief               Custom endpoint handler for device info.
 *  @param session_id    Session ID
 *  @param inbuf         Input buffer
 *  @param inlen         Input length
 *  @param outbuf        Output buffer
 *  @param outlen        Output length
 *  @param priv_data     Private data
 *  @return              ESP_OK on success
 */
static esp_err_t on_Custom_Data_Prov_Handler(uint32_t session_id, const uint8_t *p_InBuf,
                                             ssize_t inlen, uint8_t **p_OutBuf,
                                             ssize_t *p_OutLen, void *p_PrivData)
{
    if (p_InBuf) {
        ESP_LOGD(TAG, "Received custom data: %.*s", (int)inlen, (char *)p_InBuf);
    }

    const char *response = "{\"device\":\"PyroVision\",\"version\":\"1.0\"}";
    *p_OutLen = strlen(response);
    *p_OutBuf = (uint8_t *)heap_caps_malloc(*p_OutLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (*p_OutBuf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate response buffer!");
        return ESP_ERR_NO_MEM;
    }

    memcpy(*p_OutBuf, response, *p_OutLen);

    return ESP_OK;
}

esp_err_t Provisioning_Init(Network_Provisioning_Config_t *p_Config)
{
    if (_Provisioning_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    } else if (p_Config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Initializing Provisioning Manager");
    strncpy(_Provisioning_State.device_name, p_Config->Name,
            sizeof(_Provisioning_State.device_name) - 1);
    _Provisioning_State.timeout_sec = p_Config->Timeout;
    _Provisioning_State.use_captive_portal = true;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &on_Prov_Event, NULL));

    _Provisioning_State.timeout_timer = xTimerCreate("prov_timeout",
                                                     (_Provisioning_State.timeout_sec * 1000) / portTICK_PERIOD_MS,
                                                     pdFALSE,
                                                     NULL,
                                                     on_Timeout_Timer_Handler);

    _Provisioning_State.isInitialized = true;

    ESP_LOGD(TAG, "Provisioning initialized");

    return ESP_OK;
}

void Provisioning_Deinit(void)
{
    if (_Provisioning_State.isInitialized == false) {
        return;
    }

    Provisioning_Stop();

    vTaskDelay(200 / portTICK_PERIOD_MS);

    wifi_prov_mgr_deinit();

    if (_Provisioning_State.timeout_timer != NULL) {
        xTimerDelete(_Provisioning_State.timeout_timer, portMAX_DELAY);
        _Provisioning_State.timeout_timer = NULL;
    }

    esp_event_handler_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &on_Prov_Event);

    _Provisioning_State.isInitialized = false;
}

esp_err_t Provisioning_Start(void)
{
    esp_err_t Error;
    wifi_prov_mgr_config_t prov_config;
    const char *pop = NULL;
    char service_name[32];
    uint8_t mac[6];

    if (_Provisioning_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    if (_Provisioning_State.active) {
        ESP_LOGW(TAG, "Provisioning already active");
        return ESP_OK;
    }

    memset(&prov_config, 0, sizeof(prov_config));

    ESP_LOGD(TAG, "Starting Provisioning");

    if (_Provisioning_State.use_captive_portal) {
        uint8_t mac[6];
        wifi_config_t ap_config;
        Network_Server_Config_t ServerConfig;

        ESP_LOGI(TAG, "Using captive portal provisioning");

        esp_wifi_get_mac(WIFI_IF_AP, mac);

        /* Create unique SSID */
        snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), 
                 "%.26s_%02X%02X", _Provisioning_State.device_name, mac[4], mac[5]);
        ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
        ap_config.ap.channel = 1;
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ap_config.ap.max_connection = 4;

        ESP_LOGI(TAG, "Starting SoftAP with SSID: %s", ap_config.ap.ssid);

        /* Set APSTA mode to allow WiFi scanning while AP is active */
        Error = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set APSTA mode: %d", Error);
            return Error;
        }

        Error = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set AP config: %d", Error);
            return Error;
        }

        Error = esp_wifi_start();
        if (Error != ESP_OK && Error != ESP_ERR_WIFI_STATE) {
            ESP_LOGE(TAG, "Failed to start WiFi: %d", Error);
            return Error;
        }

        /* Give WiFi time to start */
        vTaskDelay(100 / portTICK_PERIOD_MS);

        /* Start HTTP server */
        ServerConfig.HTTP_Port = 80;
        ServerConfig.MaxClients = 4;
        ServerConfig.EnableCORS = true;
        ServerConfig.API_Key = NULL;

        Error = Server_Init(&ServerConfig);
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize HTTP server: %d!", Error);
            esp_wifi_stop();
            return Error;
        }

        Error = HTTP_Server_Start();
        if (Error != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server: %d!", Error);
            Server_Deinit();
            esp_wifi_stop();
            return Error;
        }

        /* Start DNS server for captive portal */
        Error = DNS_Server_Start();
        if (Error != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start DNS server: %d!", Error);
            /* Continue anyway, DNS is not critical */
        }

        _Provisioning_State.active = true;
        esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STARTED, NULL, 0, portMAX_DELAY);

        /* Get and log the actual AP IP address */
        esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if (ap_netif != NULL) {
            esp_netif_ip_info_t ip_info;
            if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK) {
                ESP_LOGI(TAG, "Captive portal started at http://" IPSTR, IP2STR(&ip_info.ip));
            } else {
                ESP_LOGI(TAG, "Captive portal started at http://192.168.4.1");
            }
        } else {
            ESP_LOGI(TAG, "Captive portal started at http://192.168.4.1");
        }

        return ESP_OK;
    }

    /* Use ESP Provisioning API (original protobuf method) */
    prov_config.scheme = wifi_prov_scheme_softap;
    prov_config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;

    Error = wifi_prov_mgr_init(prov_config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Provisioning: %d!", Error);
        return Error;
    }

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, sizeof(service_name), "%.26s_%02X%02X", _Provisioning_State.device_name, mac[4], mac[5]);

    /* Register custom endpoint */
    wifi_prov_mgr_endpoint_create("custom-data");

    Error = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1,
                                             pop, service_name, NULL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %d!", Error);
        wifi_prov_mgr_deinit();
        return Error;
    }

    wifi_prov_mgr_endpoint_register("custom-data", on_Custom_Data_Prov_Handler, NULL);
    _Provisioning_State.active = true;

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STARTED, NULL, 0, portMAX_DELAY);

    if (_Provisioning_State.timeout_timer != NULL) {
        xTimerStart(_Provisioning_State.timeout_timer, 0);
    }

    ESP_LOGD(TAG, "Provisioning started. Service name: %s", service_name);

    return ESP_OK;
}

esp_err_t Provisioning_Stop(void)
{
    if (_Provisioning_State.active == false) {
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Stopping Provisioning");

    if (_Provisioning_State.use_captive_portal) {
        /* Stop servers in correct order */
        DNS_Server_Stop();
        HTTP_Server_Stop();
        
        /* Give time for async cleanup */
        vTaskDelay(200 / portTICK_PERIOD_MS);
        
        /* Deinitialize to free memory */
        Server_Deinit();

        /* Note: Do NOT stop WiFi here - let the caller handle WiFi state transitions
         * Stopping WiFi here would disconnect active connections */
    } else {
        if (_Provisioning_State.timeout_timer != NULL) {
            xTimerStop(_Provisioning_State.timeout_timer, 0);
        }
        wifi_prov_mgr_stop_provisioning();
    }

    _Provisioning_State.active = false;

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STOPPED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

bool Provisioning_isProvisioned(void)
{
    bool isProvisioned = false;
    wifi_prov_mgr_config_t config;

    memset(&config, 0, sizeof(wifi_prov_mgr_config_t));

    config.scheme = wifi_prov_scheme_softap;
    config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;

    if (wifi_prov_mgr_init(config) == ESP_OK) {
        wifi_prov_mgr_is_provisioned(&isProvisioned);
        wifi_prov_mgr_deinit();
    }

    ESP_LOGD(TAG, "Provisioned: %s", isProvisioned ? "true" : "false");

    return isProvisioned;
}

esp_err_t Provisioning_Reset(void)
{
    ESP_LOGD(TAG, "Resetting Provisioning");

    wifi_prov_mgr_reset_provisioning();

    return ESP_OK;
}

bool Provisioning_isActive(void)
{
    return _Provisioning_State.active;
}

void Provisioning_SetNetworkTaskHandle(TaskHandle_t task_handle)
{
    _Provisioning_State.network_task_handle = task_handle;
}
