/*
 * leptonTask.c
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Main lepton task implementation.
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

#include <string.h>
#include <stdbool.h>

#include "lepton.h"
#include "leptonTask.h"
#include "Application/application.h"

#define LEPTON_TASK_STOP_REQUEST                BIT0
#define LEPTON_TASK_UPDATE_ROI_REQUEST          BIT1
#define LEPTON_TASK_UPDATE_TEMP_REQUEST         BIT2
#define LEPTON_TASK_UPDATE_UPTIME_REQUEST       BIT3
#define LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE    BIT4
#define LEPTON_TASK_UPDATE_SPOTMETER            BIT5

ESP_EVENT_DEFINE_BASE(LEPTON_EVENTS);

typedef struct {
    bool isInitialized;
    bool Running;
    bool RunTask;
    TaskHandle_t TaskHandle;
    EventGroupHandle_t EventGroup;
    uint8_t *RGB_Buffer[2];
    uint8_t CurrentReadBuffer;
    uint8_t InitializationRetries;
    SemaphoreHandle_t BufferMutex;
    QueueHandle_t RawFrameQueue;
    Lepton_FrameBuffer_t RawFrame;
    Lepton_Conf_t LeptonConf;
    Lepton_t Lepton;
    Lepton_ROI_t ROI;
    App_GUI_Screenposition_t ScreenPosition;
} Lepton_Task_State_t;

static Lepton_Task_State_t _LeptonTask_State;

static const char *TAG = "lepton_task";

static void on_GUI_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "GUI event received: ID=%d", ID);

    switch (ID) {
        case GUI_EVENT_REQUEST_ROI: {
            _LeptonTask_State.ROI.Start_Col = ((App_Settings_ROI_t *)p_Data)->x;
            _LeptonTask_State.ROI.Start_Row = ((App_Settings_ROI_t *)p_Data)->y;
            _LeptonTask_State.ROI.End_Col = ((App_Settings_ROI_t *)p_Data)->x + ((App_Settings_ROI_t *)p_Data)->w - 1;
            _LeptonTask_State.ROI.End_Row = ((App_Settings_ROI_t *)p_Data)->y + ((App_Settings_ROI_t *)p_Data)->h - 1;

            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);

            break;
        }
        case GUI_EVENT_REQUEST_FPA_AUX_TEMP: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
            break;
        }
        case GUI_EVENT_REQUEST_UPTIME: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);

            break;
        }
        case GUI_EVENT_REQUEST_PIXEL_TEMPERATURE: {
            _LeptonTask_State.ScreenPosition = *(App_GUI_Screenposition_t *)p_Data;

            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);

            break;
        }
        case GUI_EVENT_REQUEST_SPOTMETER: {
            xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SPOTMETER);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled GUI event ID: %d", ID);
            break;
        }
    }
}

/** @brief          Lepton camera task main loop.
 *  @param p_Parameters Pointer to App_Context_t structure
 */
static void Task_Lepton(void *p_Parameters)
{
    App_Context_t *App_Context;
    App_Lepton_Device_t DeviceInfo;

    esp_task_wdt_add(NULL);
    App_Context = reinterpret_cast<App_Context_t *>(p_Parameters);

    ESP_LOGD(TAG, "Lepton task started on core %d", xPortGetCoreID());

    /* Format serial number as readable string: XXXX-XXXX-XXXX-XXXX */
    snprintf(DeviceInfo.SerialNumber, sizeof(DeviceInfo.SerialNumber),
             "%02X%02X-%02X%02X-%02X%02X-%02X%02X",
             _LeptonTask_State.Lepton.SerialNumber[0], _LeptonTask_State.Lepton.SerialNumber[1],
             _LeptonTask_State.Lepton.SerialNumber[2], _LeptonTask_State.Lepton.SerialNumber[3],
             _LeptonTask_State.Lepton.SerialNumber[4], _LeptonTask_State.Lepton.SerialNumber[5],
             _LeptonTask_State.Lepton.SerialNumber[6], _LeptonTask_State.Lepton.SerialNumber[7]);
    memcpy(DeviceInfo.PartNumber, _LeptonTask_State.Lepton.PartNumber, sizeof(DeviceInfo.PartNumber));

    ESP_LOGD(TAG, "	Part number: %s", DeviceInfo.PartNumber);
    ESP_LOGD(TAG, "	Serial number: %s", DeviceInfo.SerialNumber);

    /* Wait for Lepton to stabilize after configuration */
    ESP_LOGD(TAG, "Waiting for Lepton to stabilize...");
    for (uint8_t i = 0; i < 50; i++) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    /* Wait for initial warm-up period */
    ESP_LOGD(TAG, "Waiting for initial warm-up period...");
    for (uint8_t i = 0; i < 50; i++) {
        esp_task_wdt_reset();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_CAMERA_READY, &DeviceInfo, sizeof(App_Lepton_Device_t), portMAX_DELAY);

    ESP_LOGD(TAG, "Start image capturing...");

    if (Lepton_StartCapture(&_LeptonTask_State.Lepton, _LeptonTask_State.RawFrameQueue) != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Can not start image capturing!");
        esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_CAMERA_ERROR, NULL, 0, portMAX_DELAY);
    }

    _LeptonTask_State.RunTask = true;
    while (_LeptonTask_State.RunTask) {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        /* Wait for a new raw frame with longer timeout to avoid busy waiting */
        if (xQueueReceive(_LeptonTask_State.RawFrameQueue, &_LeptonTask_State.RawFrame, 500 / portTICK_PERIOD_MS) == pdTRUE) {
            uint8_t WriteBufferIdx;
            uint8_t *WriteBuffer;
            Lepton_Telemetry_t Telemetry;

            if (_LeptonTask_State.RawFrame.Telemetry_Buffer != NULL) {
                memcpy(&Telemetry, _LeptonTask_State.RawFrame.Telemetry_Buffer, sizeof(Lepton_Telemetry_t));
                ESP_LOGD(TAG, "Telemetry - FrameCounter: %u, FPA_Temp: %uK, Housing_Temp: %uK",
                         Telemetry.FrameCounter,
                         Telemetry.FPA_Temp,
                         Telemetry.Housing_Temp);
            }

            ESP_LOGD(TAG, "Processing frame...");

            /* Determine which buffer to write to (ping-pong) */
            if (xSemaphoreTake(_LeptonTask_State.BufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                /* Find a buffer that's not currently being read */
                WriteBufferIdx = (_LeptonTask_State.CurrentReadBuffer + 1) % 2;
                WriteBuffer = _LeptonTask_State.RGB_Buffer[WriteBufferIdx];
                xSemaphoreGive(_LeptonTask_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer selection!");
                continue;
            }

            Lepton_Raw14ToRGB(_LeptonTask_State.RawFrame.Image_Buffer, WriteBuffer, _LeptonTask_State.RawFrame.Width,
                              _LeptonTask_State.RawFrame.Height);

            /* Mark buffer as ready and update read buffer index */
            if (xSemaphoreTake(_LeptonTask_State.BufferMutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
                _LeptonTask_State.CurrentReadBuffer = WriteBufferIdx;
                xSemaphoreGive(_LeptonTask_State.BufferMutex);
            } else {
                ESP_LOGW(TAG, "Failed to acquire mutex for buffer ready!");
                continue;
            }

            /* Send frame notification to network task */
            App_Lepton_FrameReady_t FrameEvent = {
                .Buffer = WriteBuffer,
                .Width = _LeptonTask_State.RawFrame.Width,
                .Height = _LeptonTask_State.RawFrame.Height,
                .Channels = 3
            };

            /* Use xQueueOverwrite to always have the latest frame */
            xQueueOverwrite(App_Context->Lepton_FrameEventQueue, &FrameEvent);
            ESP_LOGD(TAG, "Frame sent to queue successfully");
        } else {
            /* Timeout waiting for frame */
            ESP_LOGW(TAG, "No raw frame received from VoSPI");
        }

        EventBits = xEventGroupGetBits(_LeptonTask_State.EventGroup);
        if (EventBits & LEPTON_TASK_UPDATE_ROI_REQUEST) {
            ESP_LOGD(TAG, "Before SetSpotmeterROI - I2C Dev: %p", 
                    _LeptonTask_State.Lepton.Internal.CCI.I2C_Dev_Handle);
            
            if (Lepton_SetSpotmeterROI(&_LeptonTask_State.Lepton, &_LeptonTask_State.ROI) == LEPTON_ERR_OK) {
                Lepton_ROI_t CurrentROI;

                Lepton_GetSpotmeterROI(&_LeptonTask_State.Lepton, &CurrentROI);
                ESP_LOGD(TAG, "Current Lepton ROI - Start_Col: %u, Start_Row: %u, End_Col: %u, End_Row: %u",
                         CurrentROI.Start_Col,
                         CurrentROI.Start_Row,
                         CurrentROI.End_Col,
                         CurrentROI.End_Row);
            } else {
                ESP_LOGW(TAG, "Failed to update Lepton ROI!");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_ROI_REQUEST);
        } else if (EventBits & LEPTON_TASK_UPDATE_TEMP_REQUEST) {
            uint16_t FPA_Temp;
            uint16_t AUX_Temp;
            App_Lepton_Temperatures_t Temperatures;

            Lepton_GetTemp(&_LeptonTask_State.Lepton, &FPA_Temp, &AUX_Temp);

            Temperatures.FPA = (static_cast<float>(FPA_Temp) * 0.01f) - 273.0f;
            Temperatures.AUX = (static_cast<float>(AUX_Temp) * 0.01f) - 273.0f;

            esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_FPA_AUX_TEMP, &Temperatures, sizeof(App_Lepton_Temperatures_t), 0);

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_TEMP_REQUEST);
        } else if (EventBits & LEPTON_TASK_UPDATE_UPTIME_REQUEST) {
            uint32_t Uptime;

            Uptime = Lepton_GetUptime(&_LeptonTask_State.Lepton);

            esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_UPTIME, &Uptime, sizeof(uint32_t), 0);

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_UPTIME_REQUEST);
        } else if (EventBits & LEPTON_TASK_STOP_REQUEST) {
            ESP_LOGI(TAG, "Stop request received");

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

            break;
        } else if (EventBits & LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE) {
            int16_t x;
            int16_t y;
            float Temperature;

            if (_LeptonTask_State.RawFrame.Width == 0 || _LeptonTask_State.RawFrame.Height == 0) {
                ESP_LOGW(TAG, "Invalid Lepton frame dimensions, cannot get pixel temperature");
                xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
                continue;
            }

            /* Convert the screen position to the Lepton frame coordinates */
            x = (_LeptonTask_State.ScreenPosition.x * _LeptonTask_State.RawFrame.Width) / _LeptonTask_State.ScreenPosition.Width;
            y = (_LeptonTask_State.ScreenPosition.y * _LeptonTask_State.RawFrame.Height) / _LeptonTask_State.ScreenPosition.Height;

            ESP_LOGD(TAG, "Crosshair center in Lepton Frame: (%d,%d), size (%d,%d)", x, y, _LeptonTask_State.RawFrame.Width, _LeptonTask_State.RawFrame.Height);

            if (Lepton_GetPixelTemperature(&_LeptonTask_State.Lepton, _LeptonTask_State.RawFrame.Image_Buffer[(y * _LeptonTask_State.RawFrame.Width) + x],
                                           &Temperature) == LEPTON_ERR_OK) {
                esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_PIXEL_TEMPERATURE, &Temperature, sizeof(float), 0);
            } else {
                ESP_LOGW(TAG, "Failed to get pixel temperature!");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_PIXEL_TEMPERATURE);
        } else if (EventBits & LEPTON_TASK_UPDATE_SPOTMETER) {
            Lepton_Spotmeter_Float_t Spotmeter;

            ESP_LOGD(TAG, "Getting spotmeter - I2C Bus: %p, I2C Dev: %p",
                    _LeptonTask_State.Lepton.Internal.CCI.I2C_Bus_Handle,
                    _LeptonTask_State.Lepton.Internal.CCI.I2C_Dev_Handle);

            if (Lepton_GetSpotmeter(&_LeptonTask_State.Lepton, &Spotmeter) == LEPTON_ERR_OK) {
                App_Lepton_Spotmeter_t App_Lepton_Spotmeter;

                ESP_LOGD(TAG, "Spotmeter: Spot=%uK, Min=%uK, Max=%uK",
                         Spotmeter.Value,
                         Spotmeter.Min,
                         Spotmeter.Max);

                App_Lepton_Spotmeter.Min = Spotmeter.Min - 273.0f;
                App_Lepton_Spotmeter.Max = Spotmeter.Max - 273.0f;
                App_Lepton_Spotmeter.AverageTemperature = Spotmeter.Value - 273.0f;

                esp_event_post(LEPTON_EVENTS, LEPTON_EVENT_RESPONSE_SPOTMETER, &App_Lepton_Spotmeter, sizeof(App_Lepton_Spotmeter_t),
                               0);
            } else {
                ESP_LOGW(TAG, "Failed to read spotmeter!");
            }

            xEventGroupClearBits(_LeptonTask_State.EventGroup, LEPTON_TASK_UPDATE_SPOTMETER);
        }
    }

    ESP_LOGD(TAG, "Lepton task shutting down");
    Lepton_Deinit(&_LeptonTask_State.Lepton);

    _LeptonTask_State.Running = false;
    _LeptonTask_State.TaskHandle = NULL;

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t Lepton_Task_Init(void)
{
    Lepton_Error_t Lepton_Error;

    if (_LeptonTask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Lepton Task");
    _LeptonTask_State.CurrentReadBuffer = 0;
    _LeptonTask_State.InitializationRetries = 0;

    Lepton_Error = LEPTON_ERR_OK;

    /* Create event group */
    _LeptonTask_State.EventGroup = xEventGroupCreate();
    if (_LeptonTask_State.EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group!");
        return ESP_ERR_NO_MEM;
    }

    /* Create mutex for buffer synchronization */
    _LeptonTask_State.BufferMutex = xSemaphoreCreateMutex();
    if (_LeptonTask_State.BufferMutex == NULL) {
        ESP_LOGE(TAG, "Failed to create buffer mutex!");
        vEventGroupDelete(_LeptonTask_State.EventGroup);
        return ESP_ERR_NO_MEM;
    }

    /* Allocate 2 RGB buffers for ping-pong buffering */
    _LeptonTask_State.RGB_Buffer[0] = reinterpret_cast<uint8_t *>(heap_caps_malloc(LEPTON_IMAGE_WIDTH * LEPTON_IMAGE_HEIGHT
                                                                                   * 3, MALLOC_CAP_SPIRAM));
    _LeptonTask_State.RGB_Buffer[1] = reinterpret_cast<uint8_t *>(heap_caps_malloc(LEPTON_IMAGE_WIDTH * LEPTON_IMAGE_HEIGHT
                                                                                   * 3, MALLOC_CAP_SPIRAM));
    if ((_LeptonTask_State.RGB_Buffer[0] == NULL) || (_LeptonTask_State.RGB_Buffer[1] == NULL)) {
        ESP_LOGE(TAG, "Can not allocate RGB buffers!");

        if (_LeptonTask_State.RGB_Buffer[0]) {
            heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        }

        if (_LeptonTask_State.RGB_Buffer[1]) {
            heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        }

        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        vEventGroupDelete(_LeptonTask_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    /* Create internal queue to receive raw frames from VoSPI capture task */
    _LeptonTask_State.RawFrameQueue = xQueueCreate(1, sizeof(Lepton_FrameBuffer_t));
    if (_LeptonTask_State.RawFrameQueue == NULL) {
        ESP_LOGE(TAG, "Failed to create raw frame queue!");

        heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        vEventGroupDelete(_LeptonTask_State.EventGroup);

        return ESP_ERR_NO_MEM;
    }

    _LeptonTask_State.LeptonConf = LEPTON_DEFAULT_CONF;
    LEPTON_ASSIGN_FUNC(_LeptonTask_State.LeptonConf, NULL, NULL, I2CM_Write, I2CM_Read);
    LEPTON_ASSIGN_I2C_HANDLE(_LeptonTask_State.LeptonConf, DevicesManager_GetI2CBusHandle());

    ESP_LOGI(TAG, "I2C Bus Handle before init: %p", _LeptonTask_State.LeptonConf.CCI.I2C_Bus_Handle);

    Lepton_Error = Lepton_Init(&_LeptonTask_State.Lepton, &_LeptonTask_State.LeptonConf);
    if (Lepton_Error != LEPTON_ERR_OK) {
        ESP_LOGE(TAG, "Lepton initialization failed with error: %d!", Lepton_Error);

        vQueueDelete(_LeptonTask_State.RawFrameQueue);
        heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        vEventGroupDelete(_LeptonTask_State.EventGroup);

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "I2C Bus Handle after init: %p", _LeptonTask_State.Lepton.Internal.CCI.I2C_Bus_Handle);
    ESP_LOGI(TAG, "I2C Dev Handle after init: %p", _LeptonTask_State.Lepton.Internal.CCI.I2C_Dev_Handle);

    /* Use the event loop to receive control signals from other tasks */
    esp_event_handler_register(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler, NULL);

    ESP_LOGI(TAG, "Lepton Task initialized");

    _LeptonTask_State.isInitialized = true;

    return ESP_OK;
}

void Lepton_Task_Deinit(void)
{
    if (_LeptonTask_State.isInitialized == false) {
        return;
    }

    if (_LeptonTask_State.Running) {
        Lepton_Task_Stop();
    }

    ESP_LOGI(TAG, "Deinitializing Lepton Task");

    if (_LeptonTask_State.EventGroup != NULL) {
        vEventGroupDelete(_LeptonTask_State.EventGroup);
        _LeptonTask_State.EventGroup = NULL;
    }

    esp_event_handler_unregister(GUI_EVENTS, ESP_EVENT_ANY_ID, on_GUI_Event_Handler);

    Lepton_Deinit(&_LeptonTask_State.Lepton);

    if (_LeptonTask_State.BufferMutex != NULL) {
        vSemaphoreDelete(_LeptonTask_State.BufferMutex);
        _LeptonTask_State.BufferMutex = NULL;
    }

    if (_LeptonTask_State.RGB_Buffer[0] != NULL) {
        heap_caps_free(_LeptonTask_State.RGB_Buffer[0]);
        _LeptonTask_State.RGB_Buffer[0] = NULL;
    }

    if (_LeptonTask_State.RGB_Buffer[1] != NULL) {
        heap_caps_free(_LeptonTask_State.RGB_Buffer[1]);
        _LeptonTask_State.RGB_Buffer[1] = NULL;
    }

    if (_LeptonTask_State.RawFrameQueue != NULL) {
        vQueueDelete(_LeptonTask_State.RawFrameQueue);
        _LeptonTask_State.RawFrameQueue = NULL;
    }

    _LeptonTask_State.isInitialized = false;
}

esp_err_t Lepton_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t Ret;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_LeptonTask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_LeptonTask_State.Running) {
        ESP_LOGW(TAG, "Task already Running");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Starting Lepton Task");
    Ret = xTaskCreatePinnedToCore(
              Task_Lepton,
              "Task_Lepton",
              CONFIG_LEPTON_TASK_STACKSIZE,
              p_AppContext,
              CONFIG_LEPTON_TASK_PRIO,
              &_LeptonTask_State.TaskHandle,
              CONFIG_LEPTON_TASK_CORE
          );

    if (Ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Lepton Task: %d!", Ret);
        return ESP_ERR_NO_MEM;
    }

    _LeptonTask_State.Running = true;

    return ESP_OK;
}

esp_err_t Lepton_Task_Stop(void)
{
    if (_LeptonTask_State.Running == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping Lepton Task");

    /* Signal task to stop */
    xEventGroupSetBits(_LeptonTask_State.EventGroup, LEPTON_TASK_STOP_REQUEST);

    /* Wait for task to set Running = false before deleting itself */
    for (int i = 0; i < 20 && _LeptonTask_State.Running; i++) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    _LeptonTask_State.TaskHandle = NULL;
    _LeptonTask_State.Running = false;

    return ESP_OK;
}

bool Lepton_Task_isRunning(void)
{
    return _LeptonTask_State.Running;
}