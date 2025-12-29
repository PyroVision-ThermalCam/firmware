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

#include <mdns.h>

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

typedef struct {
    bool isInitialized;
    bool Running;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint32_t StartTime;
    bool RunTask;
    bool isConnected;
    Network_State_t State;
    Network_WiFiMode_t WiFi_Mode;
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
        case NETWORK_EVENT_WIFI_CONNECTED: {
            ESP_LOGD(TAG, "WiFi connected");

            /* Notify Time Manager that network is available */
            TimeManager_OnNetworkConnected();

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            ESP_LOGD(TAG, "WiFi disconnected");

            /* Notify Time Manager that network is unavailable */
            TimeManager_OnNetworkDisconnected();

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
            ESP_LOGI(TAG, "Provisioning success");

            /* Signal task to handle WiFi restart with new credentials */
            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_PROV_SUCCESS);

            break;
        }
        case NETWORK_EVENT_PROV_FAILED: {
            ESP_LOGE(TAG, "Provisioning failed!");

            /* Fall back to AP mode if configured */
            if ((_NetworkTask_State.WiFi_Mode == NETWORK_WIFI_MODE_AP) ||
                (_NetworkTask_State.WiFi_Mode == NETWORK_WIFI_MODE_APSTA)) {
                ESP_LOGI(TAG, "Falling back to AP mode");
                NetworkManager_StartAP();
            }

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            ESP_LOGW(TAG, "Provisioning timeout - stopping provisioning");

            /* Stop provisioning in task context (not timer context) */
            Provisioning_Stop();

            /* Fall back to AP mode if configured */
            if ((_NetworkTask_State.WiFi_Mode == NETWORK_WIFI_MODE_AP) ||
                (_NetworkTask_State.WiFi_Mode == NETWORK_WIFI_MODE_APSTA)) {
                ESP_LOGI(TAG, "Falling back to AP mode");
                NetworkManager_StartAP();
            }
            break;
        }
        case NETWORK_EVENT_OPEN_WIFI_REQUEST: {
            ESP_LOGI(TAG, "Open WiFi request received");

            xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief          Network task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Network(void *p_Parameters)
{
    App_Context_t *App_Context;

    esp_task_wdt_add(NULL);

    App_Context = reinterpret_cast<App_Context_t *>(p_Parameters);
    ESP_LOGD(TAG, "Network task started on core %d", xPortGetCoreID());

    /*
    do {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_NetworkTask_State.EventGroup);
        if (EventBits & NETWORK_TASK_OPEN_WIFI_REQUEST) {

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_OPEN_WIFI_REQUEST);
            break;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    } while (true);*/

    /* Try to load stored WiFi credentials */
    NetworkManager_LoadCredentials();
    if ((App_Context->Network_Config.Prov_Method != NETWORK_PROV_NONE) && (Provisioning_isProvisioned() == false) &&
        ((strlen(App_Context->Network_Config.STA_Config.ssid) == 0))) {

        /* No credentials, start provisioning */
        ESP_LOGI(TAG, "No credentials found, starting provisioning");

        Provisioning_Start();

        _NetworkTask_State.State = NETWORK_STATE_PROVISIONING;
    } else {
        /* Start WiFi based on mode */
        switch (App_Context->Network_Config.WiFi_Mode) {
            case NETWORK_WIFI_MODE_STA:
                NetworkManager_StartSTA();
                break;
            case NETWORK_WIFI_MODE_AP:
                NetworkManager_StartAP();
                break;
            case NETWORK_WIFI_MODE_APSTA:
                NetworkManager_StartAPSTA();
                break;
        }
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

            if (NetworkManager_StartServer(&App_Context->Server_Config) == ESP_OK) {
                ESP_LOGI(TAG, "HTTP/WebSocket server started");

                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_STARTED, NULL, 0, portMAX_DELAY);
            } else {
                ESP_LOGE(TAG, "Failed to start HTTP/WebSocket server");

                esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SERVER_ERROR, NULL, 0, portMAX_DELAY);
            }

            xEventGroupClearBits(_NetworkTask_State.EventGroup, NETWORK_TASK_WIFI_CONNECTED);
        } else if (EventBits & NETWORK_TASK_WIFI_DISCONNECTED) {
            ESP_LOGD(TAG, "Handling WiFi disconnection");

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

    Error = NetworkManager_Init(&p_AppContext->Network_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi manager: %d!", Error);

        esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
        vEventGroupDelete(_NetworkTask_State.EventGroup);

        return Error;
    }

    Error = Provisioning_Init(p_AppContext->Network_Config.Prov_Method, &p_AppContext->Network_Config);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning: %d!", Error);
        /* Continue anyway, provisioning is optional */
    }

    _NetworkTask_State.State = NETWORK_STATE_IDLE;
    _NetworkTask_State.WiFi_Mode = p_AppContext->Network_Config.WiFi_Mode;
    _NetworkTask_State.isInitialized = true;

    ESP_LOGI(TAG, "Network Task initialized");

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

/** @brief              Start the network task.
 *  @param p_AppContext Pointer to application context
 *  @return             ESP_OK on success, error code otherwise
 */
esp_err_t Network_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Ret;

    if (_NetworkTask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_NetworkTask_State.Running) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Network Task");
    Ret = xTaskCreatePinnedToCore(
              Task_Network,
              "Task_Network",
              CONFIG_NETWORK_TASK_STACKSIZE,
              p_AppContext,
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

    /* Signal task to stop */
    xEventGroupSetBits(_NetworkTask_State.EventGroup, NETWORK_TASK_STOP_REQUEST);

    /* Wait for task to set Running = false before deleting itself */
    for (uint8_t i = 0; i < 20 && _NetworkTask_State.Running; i++) {
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