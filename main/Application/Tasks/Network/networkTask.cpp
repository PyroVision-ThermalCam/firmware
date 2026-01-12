/*
 * networkTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network task implementation.
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
#include <esp_task_wdt.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <nvs_flash.h>

#include <string.h>

#include "networkTask.h"
#include "Application/Manager/managers.h"
#include "Application/Tasks/GUI/guiTask.h"

#define NETWORK_TASK_STOP_REQUEST           BIT0
#define NETWORK_TASK_BROADCAST_FRAME        BIT1
#define NETWORK_TASK_PROV_SUCCESS           BIT2
#define NETWORK_TASK_WIFI_CONNECTED         BIT3
#define NETWORK_TASK_WIFI_DISCONNECTED      BIT4
#define NETWORK_TASK_PROV_TIMEOUT           BIT5
#define NETWORK_TASK_OPEN_WIFI_REQUEST      BIT6
#define NETWORK_TASK_SNTP_TIMEZONE_SET      BIT7
#define NETWORK_TASK_WIFI_CREDENTIALS_UPDATED BIT8

typedef struct {
    bool isInitialized;
    bool isConnected;
    bool Running;
    bool RunTask;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint32_t StartTime;
    Network_State_t State;
    const char *Timezone;
    App_Context_t *AppContext;
} Network_Task_State_t;

static Network_Task_State_t _NetworkTask_State;

static const char *TAG = "network_task";

/** @brief                  Network event handler for task coordination.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case NETWORK_EVENT_CREDENTIALS_UPDATED: {
            Network_WiFi_Credentials_t *Credentials = (Network_WiFi_Credentials_t *)p_Data;

            ESP_LOGD(TAG, "WiFi credentials updated");

            memcpy(&_NetworkTask_State.AppContext->Settings.WiFi.SSID,
                   Credentials->SSID,
                   sizeof(_NetworkTask_State.AppContext->Settings.WiFi.SSID));
            memcpy(&_NetworkTask_State.AppContext->Settings.WiFi.Password,
                   Credentials->Password,
                   sizeof(_NetworkTask_State.AppContext->Settings.WiFi.Password));

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CREDENTIALS_UPDATED);

            break;
        }
        case NETWORK_EVENT_SET_TZ: {
            ESP_LOGD(TAG, "Timezone set");

            _NetworkTask_State.Timezone = static_cast<const char *>(p_Data);

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_SNTP_TIMEZONE_SET);

            break;
        }
        case NETWORK_EVENT_WIFI_CONNECTED: {
            ESP_LOGD(TAG, "WiFi connected");

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            ESP_LOGD(TAG, "WiFi disconnected");

            _NetworkTask_State.State = NETWORK_STATE_DISCONNECTED;
            _NetworkTask_State.isConnected = false;

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);

            break;
        }
        case NETWORK_EVENT_WIFI_GOT_IP: {
            _NetworkTask_State.State = NETWORK_STATE_CONNECTED;
            _NetworkTask_State.isConnected = true;

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            ESP_LOGD(TAG, "Provisioning success");

            /* Signal task to handle WiFi restart with new credentials */
            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);

            break;
        }
        case NETWORK_EVENT_PROV_FAILED: {
            ESP_LOGE(TAG, "Provisioning failed!");

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            ESP_LOGW(TAG, "Provisioning timeout - stopping provisioning");

            /* Stop provisioning in task context (not timer context) */
            Provisioning_Stop();

            break;
        }
        case NETWORK_EVENT_OPEN_WIFI_REQUEST: {
            ESP_LOGD(TAG, "Open WiFi request received");

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief              Network task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Network(void *p_Parameters)
{
    esp_task_wdt_add(NULL);

    ESP_LOGD(TAG, "Network task started on core %d", xPortGetCoreID());

    ESP_LOGI(TAG, "Autoconnect is %s", (_NetworkTask_State.AppContext->Settings.WiFi.AutoConnect) ? "enabled" : "disabled");
    /*
    if(App_Context->Settings.WiFi.AutoConnect == false) {
        do {
            EventBits_t EventBits;

            esp_task_wdt_reset();

            EventBits = xEventGroupGetBits(_NetworkTask_State.EventGroup);
            if (EventBits & NETWORK_TASK_OPEN_WIFI_REQUEST) {

                xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);
                break;
            }

            vTaskDelay(100 / portTICK_PERIOD_MS);
        } while (true);
    }*/

    if ((Provisioning_isProvisioned() == false) &&
        (strlen(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) == 0)) {
        ESP_LOGI(TAG, "No credentials found, starting provisioning");

        Provisioning_Start();

        _NetworkTask_State.State = NETWORK_STATE_PROVISIONING;
    } else {
        NetworkManager_StartSTA();
    }

    _NetworkTask_State.RunTask = true;
    while (_NetworkTask_State.RunTask) {
        uint32_t NotificationValue = 0;
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Check for task notifications (from timer callbacks) */
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &NotificationValue, 0) == pdTRUE) {
            if (NotificationValue & 0x01) {
                /* Provisioning timeout notification */
                ESP_LOGW(TAG, "Provisioning timeout");

                xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_TIMEOUT);
            }
        }

        EventBits = xEventGroupGetBits(_NetworkTask_State.EventGroup);
        if (EventBits & NETWORK_TASK_STOP_REQUEST) {
            ESP_LOGD(TAG, "Stop request received");

            _NetworkTask_State.RunTask = false;

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

            break;
        } else if (EventBits & NETWORK_TASK_WIFI_CONNECTED) {
            ESP_LOGD(TAG, "Handling WiFi connection");

            /* Notify Time Manager that network is available */
            TimeManager_OnNetworkConnected();

            if (NetworkManager_StartServer(&_NetworkTask_State.AppContext->Server_Config) == ESP_OK) {
                ESP_LOGI(TAG, "HTTP/WebSocket server started");

                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_STARTED, NULL, 0, portMAX_DELAY);
            } else {
                ESP_LOGE(TAG, "Failed to start HTTP/WebSocket server");

                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_ERROR, NULL, 0, portMAX_DELAY);
            }

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);
        } else if (EventBits & NETWORK_TASK_WIFI_DISCONNECTED) {
            ESP_LOGD(TAG, "Handling WiFi disconnection");

            /* Notify Time Manager that network is unavailable */
            TimeManager_OnNetworkDisconnected();

            Provisioning_Start();

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_DISCONNECTED);
        } else if (EventBits & NETWORK_TASK_PROV_SUCCESS) {
            ESP_LOGD(TAG, "Handling provisioning success");

            Provisioning_Stop();

            /* Restart WiFi in STA mode with new credentials */
            NetworkManager_Stop();
            NetworkManager_StartSTA();

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);
        } else if (EventBits & NETWORK_TASK_PROV_TIMEOUT) {
            ESP_LOGW(TAG, "Handling provisioning timeout");

            Provisioning_Stop();

            /* Post event for GUI */
            esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_PROV_TIMEOUT, NULL, 0, portMAX_DELAY);

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_TIMEOUT);
        } else if (EventBits & NETWORK_TASK_SNTP_TIMEZONE_SET) {

            memcpy(&_NetworkTask_State.AppContext->Settings.System.Timezone, _NetworkTask_State.Timezone,
                   sizeof(_NetworkTask_State.AppContext->Settings.System.Timezone));
            SettingsManager_Save(&_NetworkTask_State.AppContext->Settings);
            TimeManager_SetTimezone(_NetworkTask_State.AppContext->Settings.System.Timezone);

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_SNTP_TIMEZONE_SET);
        } else if (EventBits & NETWORK_TASK_WIFI_CREDENTIALS_UPDATED) {
            SettingsManager_Save(&_NetworkTask_State.AppContext->Settings);

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CREDENTIALS_UPDATED);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ESP_LOGD(TAG, "Network task shutting down");
    Provisioning_Stop();
    NetworkManager_Stop();

    _NetworkTask_State.Running = false;
    _NetworkTask_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Network_Task_Init(App_Context_t *p_AppContext)
{
    esp_err_t Error;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_NetworkTask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing network task");

    _NetworkTask_State.AppContext = p_AppContext;

    /* Initialize NVS */
    Error = nvs_flash_init();
    if ((Error == ESP_ERR_NVS_NO_FREE_PAGES) || (Error == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        Error = nvs_flash_init();
    }

    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS flash: %d!", Error);
        return Error;
    }

    /* Create event group */
    _NetworkTask_State.EventGroup = xEventGroupCreate();
    if (_NetworkTask_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        return ESP_ERR_NO_MEM;
    }

    /* Register event handler for network events */
    Error = esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %d!", Error);

        vEventGroupDelete(_NetworkTask_State.EventGroup);

        return Error;
    }

    /* Copy the required WiFi settings from settings to network config */
    strncpy(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID, p_AppContext->Settings.WiFi.SSID,
            sizeof(_NetworkTask_State.AppContext->STA_Config.Credentials.SSID) - 1);
    strncpy(_NetworkTask_State.AppContext->STA_Config.Credentials.Password, p_AppContext->Settings.WiFi.Password,
            sizeof(_NetworkTask_State.AppContext->STA_Config.Credentials.Password) - 1);
    _NetworkTask_State.AppContext->STA_Config.MaxRetries = p_AppContext->Settings.WiFi.MaxRetries;
    _NetworkTask_State.AppContext->STA_Config.RetryInterval = p_AppContext->Settings.WiFi.RetryInterval;

    /* Copy the required Provisioning settings from settings to network config */
    strncpy(_NetworkTask_State.AppContext->STA_Config.ProvConfig.DeviceName,
            p_AppContext->Settings.ProvConfig.DeviceName,
            sizeof(_NetworkTask_State.AppContext->STA_Config.ProvConfig.DeviceName) - 1);
    strncpy(_NetworkTask_State.AppContext->STA_Config.ProvConfig.PoP,
            p_AppContext->Settings.ProvConfig.PoP,
            sizeof(_NetworkTask_State.AppContext->STA_Config.ProvConfig.PoP) - 1);
    _NetworkTask_State.AppContext->STA_Config.ProvConfig.Timeout = p_AppContext->Settings.ProvConfig.Timeout;

    Error = NetworkManager_Init(&_NetworkTask_State.AppContext->STA_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi manager: %d!", Error);

        esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
        vEventGroupDelete(_NetworkTask_State.EventGroup);

        return Error;
    }

    Error = Provisioning_Init(&p_AppContext->STA_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning: %d!", Error);
        /* Continue anyway, provisioning is optional */
    }

    _NetworkTask_State.AppContext = p_AppContext;
    _NetworkTask_State.State = NETWORK_STATE_IDLE;
    _NetworkTask_State.isInitialized = true;

    ESP_LOGD(TAG, "Network Task initialized");

    return ESP_OK;
}

void Network_Task_Deinit(void)
{
    if (_NetworkTask_State.isInitialized == false) {
        return;
    }

    ESP_LOGI(TAG, "Deinitializing Network Task");

    Network_Task_Stop();
    Provisioning_Deinit();
    NetworkManager_Deinit();

    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);

    if (_NetworkTask_State.EventGroup != NULL) {
        vEventGroupDelete(_NetworkTask_State.EventGroup);
        _NetworkTask_State.EventGroup = NULL;
    }

    _NetworkTask_State.isInitialized = false;
}

esp_err_t Network_Task_Start(void)
{
    BaseType_t Ret;

    if (_NetworkTask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_NetworkTask_State.Running) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Starting Network Task");
    Ret = xTaskCreatePinnedToCore(
              Task_Network,
              "Task_Network",
              CONFIG_NETWORK_TASK_STACKSIZE,
              NULL,
              CONFIG_NETWORK_TASK_PRIO,
              &_NetworkTask_State.TaskHandle,
              CONFIG_NETWORK_TASK_CORE
          );

    if (Ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task!");
        return ESP_ERR_NO_MEM;
    }

    /* Register task handle with provisioning for timeout notification */
    Provisioning_SetNetworkTaskHandle(_NetworkTask_State.TaskHandle);
    _NetworkTask_State.Running = true;
    _NetworkTask_State.StartTime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    return ESP_OK;
}

esp_err_t Network_Task_Stop(void)
{
    if (_NetworkTask_State.Running == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Network Task");

    xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

    /* Wait for task to set Running = false before deleting itself */
    for (uint8_t i = 0; (i < 20) && _NetworkTask_State.Running; i++) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    _NetworkTask_State.TaskHandle = NULL;
    _NetworkTask_State.Running = false;

    return ESP_OK;
}

bool Network_Task_isRunning(void)
{
    return _NetworkTask_State.Running;
}