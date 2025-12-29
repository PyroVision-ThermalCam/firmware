/*
 * sntp.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: SNTP management implementation.
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
#include <esp_sntp.h>
#include <esp_event.h>

#include "sntp.h"
#include "../networkManager.h"

#define SNTP_TZ_MAX_LEN 64

static char _SNTP_Timezone[SNTP_TZ_MAX_LEN] = "CET-1CEST,M3.5.0,M10.5.0/3";

static const char *TAG = "sntp";

/** @brief                  SNTP event handler for task coordination.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_SNTP_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    switch (ID) {
        case NETWORK_EVENT_SET_TZ: {
            size_t Len = strlen((const char *)p_Data);
            if (Len < SNTP_TZ_MAX_LEN) {
                memcpy(_SNTP_Timezone, (const char *)p_Data, Len + 1);
                setenv("TZ", _SNTP_Timezone, 1);
                tzset();
            } else {
                ESP_LOGE(TAG, "Timezone string too long!");
            }

            break;
        }
        default: {
            break;
        }
    }
}

/** @brief      SNTP time synchronization callback.
 *  @param p_tv Pointer to timeval structure with synchronized time
 */
static void on_SNTP_Time_Sync(struct timeval *p_tv)
{
    ESP_LOGD(TAG, "Time synchronized via SNTP");

    esp_event_post(NETWORK_EVENTS, NETWORK_EVENT_SNTP_SYNCED, p_tv, sizeof(struct timeval), portMAX_DELAY);
}

esp_err_t SNTP_Init(void)
{
    esp_err_t Error;

    Error = esp_event_handler_register(NETWORK_EVENTS, NETWORK_EVENT_SET_TZ, on_SNTP_Event_Handler, NULL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %d!", Error);
        return Error;
    }

    setenv("TZ", _SNTP_Timezone, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(on_SNTP_Time_Sync);
    esp_sntp_init();

    return ESP_OK;
}

esp_err_t SNTP_Deinit(void)
{
    esp_sntp_stop();
    return esp_event_handler_unregister(NETWORK_EVENTS, NETWORK_EVENT_SET_TZ, on_SNTP_Event_Handler);
}

esp_err_t SNTP_GetTime(uint8_t Retries)
{
    time_t Now;
    struct tm TimeInfo;
    uint8_t Retry = 0;

    ESP_LOGD(TAG, "Initializing SNTP");

    memset(&TimeInfo, 0, sizeof(struct tm));

    while ((TimeInfo.tm_year < (2016 - 1900)) && (++Retry < Retries)) {
        ESP_LOGD(TAG, "Waiting for system time... (%d/%d)", Retry, Retries);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&Now);
        localtime_r(&Now, &TimeInfo);
    }

    if (Retry == Retries) {
        ESP_LOGW(TAG, "Failed to synchronize time!");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Time synchronized successfully");
    time(&Now);
    localtime_r(&Now, &TimeInfo);

    return ESP_OK;
}