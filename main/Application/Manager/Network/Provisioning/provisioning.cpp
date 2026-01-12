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

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>
#include <wifi_provisioning/scheme_softap.h>

#include <string.h>

#include "../networkManager.h"
#include "provisioning.h"

typedef struct {
    bool isInitialized;
    bool active;
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

                ESP_LOGD(TAG, "Credentials - SSID: %s, Password: %s", Credentials.SSID, Credentials.Password);
            } else {
                ESP_LOGE(TAG, "No WiFi credentials available!");
            }

            NetworkManager_SetCredentials(&Credentials);

            break;
        }
        case WIFI_PROV_END: {
            ESP_LOGD(TAG, "Provisioning ended");

            wifi_prov_mgr_deinit();
            wifi_prov_scheme_ble_event_cb_free_btdm(NULL, WIFI_PROV_END, NULL);

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
    *p_OutBuf = (uint8_t *)malloc(*p_OutLen);

    if (*p_OutBuf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate response buffer!");
        return ESP_ERR_NO_MEM;
    }

    memcpy(*p_OutBuf, response, *p_OutLen);

    return ESP_OK;
}

esp_err_t Provisioning_Init(Network_WiFi_STA_Config_t *p_Config)
{
    if (_Provisioning_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing Provisioning Manager");
    strncpy(_Provisioning_State.device_name, p_Config->ProvConfig.DeviceName,
            sizeof(_Provisioning_State.device_name) - 1);
    strncpy(_Provisioning_State.pop, p_Config->ProvConfig.PoP, sizeof(_Provisioning_State.pop) - 1);
    _Provisioning_State.timeout_sec = p_Config->ProvConfig.Timeout;

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

    prov_config.scheme = wifi_prov_scheme_ble;
    prov_config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
    prov_config.app_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;

    Error = wifi_prov_mgr_init(prov_config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Provisioning: %d!", Error);
        return Error;
    }

#ifdef CONFIG_NETWORK_PROV_SECURITY_VERSION
    int security = CONFIG_NETWORK_PROV_SECURITY_VERSION;
#else
    int security = 1;
#endif

    if ((security == 1) && (strlen(_Provisioning_State.pop) > 0)) {
        pop = _Provisioning_State.pop;
    }

    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, sizeof(service_name), "%.26s_%02X%02X", _Provisioning_State.device_name, mac[4], mac[5]);

    uint8_t custom_service_uuid[] = {
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };
    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);

    /* Register custom endpoint */
    wifi_prov_mgr_endpoint_create("custom-data");

    Error = wifi_prov_mgr_start_provisioning((wifi_prov_security_t)security,
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

    if (_Provisioning_State.timeout_timer != NULL) {
        xTimerStop(_Provisioning_State.timeout_timer, 0);
    }

    wifi_prov_mgr_stop_provisioning();

    _Provisioning_State.active = false;

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_STOPPED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

bool Provisioning_isProvisioned(void)
{
    bool isProvisioned = false;
    wifi_prov_mgr_config_t config;

    memset(&config, 0, sizeof(wifi_prov_mgr_config_t));

    config.scheme = wifi_prov_scheme_ble;
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
