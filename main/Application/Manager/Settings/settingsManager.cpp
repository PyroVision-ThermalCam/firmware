/*
 * settingsManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Persistent settings management using NVS storage.
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
#include <esp_mac.h>
#include <esp_efuse.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <string.h>

#include "settingsManager.h"

static const char *TAG = "settings_manager";

ESP_EVENT_DEFINE_BASE(SETTINGS_EVENTS);

/** @brief Settings Manager state.
 */
typedef struct {
    bool isInitialized;
    bool PendingChanges;
    nvs_handle_t NVS_Handle;
    App_Settings_t Settings;
    SemaphoreHandle_t Mutex;
} SettingsManager_State_t;

static SettingsManager_State_t _State = {
    .isInitialized = false,
    .PendingChanges = false,
    .NVS_Handle = 0,
    .Settings = {0},
    .Mutex = NULL,
};

/** @brief Initialize settings with factory defaults.
 */
static void _SettingsManager_InitDefaults(App_Settings_t *p_Settings)
{
    uint8_t Mac[6];

    memset(p_Settings, 0, sizeof(App_Settings_t));

    p_Settings->Version = SETTINGS_VERSION;

    /* Spotmeter defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].Type = ROI_TYPE_SPOTMETER;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].x = 60;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].y = 40;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].w = 40;
    p_Settings->Lepton.ROI[ROI_TYPE_SPOTMETER].h = 40;

    /* Scene statistics defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].Type = ROI_TYPE_SCENE;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].x = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].y = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].w = 160;
    p_Settings->Lepton.ROI[ROI_TYPE_SCENE].h = 120;

    /* AGC defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].Type = ROI_TYPE_AGC;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].x = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].y = 0;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].w = 160;
    p_Settings->Lepton.ROI[ROI_TYPE_AGC].h = 120;

    /* Video focus defaults */
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].Type = ROI_TYPE_VIDEO_FOCUS;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].x = 1;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].y = 1;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].w = 157;
    p_Settings->Lepton.ROI[ROI_TYPE_VIDEO_FOCUS].h = 157;

    p_Settings->Lepton.EnableSceneStatistics = true;
    p_Settings->Lepton.Emissivity = 95;

    /* WiFi defaults */
    strncpy(p_Settings->WiFi.SSID, "", sizeof(p_Settings->WiFi.SSID));
    strncpy(p_Settings->WiFi.Password, "", sizeof(p_Settings->WiFi.Password));
    p_Settings->WiFi.AutoConnect = true;

    /* Display defaults */
    p_Settings->Display.Brightness = 80;
    p_Settings->Display.ScreenTimeout = 0;

    /* System defaults */
    if (esp_efuse_mac_get_default(Mac) == ESP_OK) {
        snprintf(p_Settings->System.DeviceName, sizeof(p_Settings->System.DeviceName), "PyroVision-%02X%02X%02X%02X%02X%02X",
                 Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
    } else {
        snprintf(p_Settings->System.DeviceName, sizeof(p_Settings->System.DeviceName), "PyroVision");
        ESP_LOGW(TAG, "Failed to get MAC address, using default name");
    }

    p_Settings->System.SDCard_AutoMount = true;
    p_Settings->System.Bluetooth_Enabled = false;
    strncpy(p_Settings->System.Timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(p_Settings->System.Timezone));
}

esp_err_t SettingsManager_Init(void)
{
    esp_err_t Error;

    if (_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing Settings Manager...");

    ESP_ERROR_CHECK(nvs_flash_init());

    _State.Mutex = xSemaphoreCreateMutex();
    if (_State.Mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex!");
        return ESP_ERR_NO_MEM;
    }

    Error = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %d!", Error);
        vSemaphoreDelete(_State.Mutex);
        return Error;
    }

    _State.isInitialized = true;
    _State.PendingChanges = false;

    Error = SettingsManager_Load(&_State.Settings);
    if (Error == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No settings found, using factory defaults");
        _SettingsManager_InitDefaults(&_State.Settings);
        SettingsManager_Save(&_State.Settings);
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings: %d!", Error);
        _SettingsManager_InitDefaults(&_State.Settings);
    }

    ESP_LOGI(TAG, "Settings Manager initialized (version %lu)", _State.Settings.Version);

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_LOADED, &_State.Settings, sizeof(App_Settings_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_Deinit(void)
{
    if (_State.isInitialized == false) {
        return ESP_OK;
    }

    if (_State.PendingChanges) {
        SettingsManager_Commit();
    }

    nvs_close(_State.NVS_Handle);
    vSemaphoreDelete(_State.Mutex);

    _State.isInitialized = false;

    ESP_LOGI(TAG, "Settings Manager deinitialized");

    return ESP_OK;
}

esp_err_t SettingsManager_Load(App_Settings_t *p_Settings)
{
    esp_err_t Error;
    size_t RequiredSize;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_get_blob(_State.NVS_Handle, "settings", NULL, &RequiredSize);
    if (Error == ESP_ERR_NVS_NOT_FOUND) {
        xSemaphoreGive(_State.Mutex);
        return ESP_ERR_NVS_NOT_FOUND;
    } else if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get settings size: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    if (RequiredSize != sizeof(App_Settings_t)) {
        ESP_LOGW(TAG, "Settings size mismatch (expected %u, got %u), using defaults",
                 sizeof(App_Settings_t), RequiredSize);
        xSemaphoreGive(_State.Mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    Error = nvs_get_blob(_State.NVS_Handle, "settings", p_Settings, &RequiredSize);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    /* Verify version */
    if (p_Settings->Version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings version mismatch (expected %d, got %lu), migration needed",
                 SETTINGS_VERSION, p_Settings->Version);
        /* TODO: Implement migration logic here */
    }

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Settings loaded from NVS");

    return ESP_OK;
}

esp_err_t SettingsManager_Save(const App_Settings_t *p_Settings)
{
    esp_err_t Error;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_set_blob(_State.NVS_Handle, "settings", p_Settings, sizeof(App_Settings_t));
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    Error = nvs_commit(_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    _State.PendingChanges = false;

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Settings saved to NVS");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_SAVED, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_Get(App_Settings_t *p_Settings)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);
    memcpy(p_Settings, &_State.Settings, sizeof(App_Settings_t));
    xSemaphoreGive(_State.Mutex);

    return ESP_OK;
}

esp_err_t SettingsManager_SetLepton(const App_Settings_Lepton_t *p_Lepton)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Lepton == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    memcpy(&_State.Settings.Lepton, p_Lepton, sizeof(App_Settings_Lepton_t));
    _State.PendingChanges = true;

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Lepton settings updated");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_LEPTON_CHANGED, p_Lepton,
                   sizeof(App_Settings_Lepton_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_SetWiFi(const App_Settings_WiFi_t *p_WiFi)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_WiFi == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    memcpy(&_State.Settings.WiFi, p_WiFi, sizeof(App_Settings_WiFi_t));
    _State.PendingChanges = true;

    xSemaphoreGive(_State.Mutex);

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_WIFI_CHANGED, p_WiFi,
                   sizeof(App_Settings_WiFi_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_SetDisplay(const App_Settings_Display_t *p_Display)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_Display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    memcpy(&_State.Settings.Display, p_Display, sizeof(App_Settings_Display_t));
    _State.PendingChanges = true;

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Display settings updated (Brightness: %d%%, Timeout: %ds)",
             p_Display->Brightness, p_Display->ScreenTimeout);

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_DISPLAY_CHANGED, p_Display,
                   sizeof(App_Settings_Display_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_SetSystem(const App_Settings_System_t *p_System)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (p_System == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    memcpy(&_State.Settings.System, p_System, sizeof(App_Settings_System_t));
    _State.PendingChanges = true;

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "System settings updated (Device: %s, SD Auto: %d)",
             p_System->DeviceName, p_System->SDCard_AutoMount);

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_SYSTEM_CHANGED, p_System,
                   sizeof(App_Settings_System_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_ResetToDefaults(void)
{
    esp_err_t Error;

    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Resetting settings to factory defaults");

    xSemaphoreTake(_State.Mutex, portMAX_DELAY);

    Error = nvs_erase_key(_State.NVS_Handle, "settings");
    if (Error != ESP_OK && Error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    Error = nvs_commit(_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit erase: %d!", Error);
        xSemaphoreGive(_State.Mutex);
        return Error;
    }

    _SettingsManager_InitDefaults(&_State.Settings);
    _State.PendingChanges = false;

    xSemaphoreGive(_State.Mutex);

    SettingsManager_Save(&_State.Settings);

    ESP_LOGI(TAG, "Settings reset to factory defaults");

    esp_event_post(SETTINGS_EVENTS, SETTINGS_EVENT_CHANGED, &_State.Settings,
                   sizeof(App_Settings_t), portMAX_DELAY);

    return ESP_OK;
}

esp_err_t SettingsManager_GetDefaults(App_Settings_t *p_Settings)
{
    if (p_Settings == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    _SettingsManager_InitDefaults(p_Settings);

    return ESP_OK;
}

esp_err_t SettingsManager_Commit(void)
{
    if (_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_State.PendingChanges == false) {
        ESP_LOGD(TAG, "No pending changes to commit");
        return ESP_OK;
    }

    return SettingsManager_Save(&_State.Settings);
}

bool SettingsManager_HasPendingChanges(void)
{
    return _State.PendingChanges;
}
