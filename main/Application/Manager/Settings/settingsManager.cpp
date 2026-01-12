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
#include <esp_littlefs.h>

#include <nvs_flash.h>
#include <nvs.h>

#include <string.h>
#include <sys/stat.h>
#include <cJSON.h>

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
static void SettingsManager_InitDefaults(App_Settings_t *p_Settings)
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

    /* No emissiviy values available */
    p_Settings->Lepton.EmissivityCount = 1;
    p_Settings->Lepton.EmissivityPresets[0].Value = 100.0f;
    strncpy(p_Settings->Lepton.EmissivityPresets[0].Description, "Unknown", sizeof(p_Settings->Lepton.EmissivityPresets[0].Description));

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

/** @brief Initialize the settings presets from the NVS by using a config JSON file.
 */
static esp_err_t SettingsManager_LoadDefaultsFromJSON(void)
{
    uint8_t ConfigLoaded = 0;
    esp_err_t Error;
    cJSON *json = NULL;
    cJSON *lepton = NULL;
    cJSON *emissivity_array = NULL;
    size_t Total = 0;
    size_t Used = 0;
    size_t BytesRead = 0;
    FILE* SettingsFile = NULL;
    char *SettingsBuffer = NULL;
    long FileSize;
    esp_vfs_littlefs_conf_t LittleFS_Config = {
      .base_path = "/littlefs",
      .partition_label = "storage",
      .format_if_mount_failed = true,
      .dont_mount = false
    };

    Error = nvs_get_u8(_State.NVS_Handle, "config_loaded", &ConfigLoaded);
    if ((Error == ESP_OK) && (ConfigLoaded == true)) {
        ESP_LOGI(TAG, "Default config already loaded, skipping");
        //return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing LittleFS");

    Error = esp_vfs_littlefs_register(&LittleFS_Config);
    if (Error != ESP_OK) {
        if (Error == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem!");
        } else if (Error == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition!");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS: %d!", Error);
        }

        return ESP_FAIL;
    }

    Error = esp_littlefs_info(LittleFS_Config.partition_label, &Total, &Used);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information: %d!", Error);
    } else {
        ESP_LOGI(TAG, "Partition size: total: %zu, used: %zu", Total, Used);
    }

    ESP_LOGI(TAG, "Opening settings file...");
    SettingsFile = fopen("/littlefs/default_settings.json", "r");
    if (SettingsFile == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading!");
        esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

        return ESP_FAIL;
    }

    /* Get file size */
    fseek(SettingsFile, 0, SEEK_END);
    FileSize = ftell(SettingsFile);
    fseek(SettingsFile, 0, SEEK_SET);

    if (FileSize <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld!", FileSize);
        fclose(SettingsFile);
        esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File size: %ld bytes", FileSize);

    /* Allocate buffer for file content */
    SettingsBuffer = (char*)heap_caps_malloc(FileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (SettingsBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file buffer!");
        fclose(SettingsFile);
        esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

        return ESP_ERR_NO_MEM;
    }

    /* Read file content */
    BytesRead = fread(SettingsBuffer, 1, FileSize, SettingsFile);
    fclose(SettingsFile);
    SettingsFile = NULL;

    if (BytesRead != FileSize) {
        ESP_LOGE(TAG, "Failed to read file (read %zu of %ld bytes)", BytesRead, FileSize);
        heap_caps_free(SettingsBuffer);

        return ESP_FAIL;
    }

    esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

    ESP_LOGI(TAG, "File read successfully, parsing JSON...");

    /* Parse JSON */
    json = cJSON_Parse(SettingsBuffer);
    heap_caps_free(SettingsBuffer);
    SettingsBuffer = NULL;

    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON Parse Error: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON Parse Error: Unknown");
        }

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "JSON parsed successfully");

    /* Extract lepton settings */
    lepton = cJSON_GetObjectItem(json, "lepton");
    if (lepton != NULL) {
        emissivity_array = cJSON_GetObjectItem(lepton, "emissivity");
        if (cJSON_IsArray(emissivity_array)) {
            _State.Settings.Lepton.EmissivityCount = cJSON_GetArraySize(emissivity_array);

            ESP_LOGI(TAG, "Found %d emissivity presets in JSON", _State.Settings.Lepton.EmissivityCount);

            for (uint32_t i = 0; i < _State.Settings.Lepton.EmissivityCount; i++) {
                cJSON *preset = cJSON_GetArrayItem(emissivity_array, i);
                cJSON *name = cJSON_GetObjectItem(preset, "name");
                cJSON *value = cJSON_GetObjectItem(preset, "value");

                if (cJSON_IsString(name) && cJSON_IsNumber(value)) {
                    _State.Settings.Lepton.EmissivityPresets[i].Value = (float)(value->valuedouble);

                    /* Cap the emissivity value between 0 and 100 */
                    if (_State.Settings.Lepton.EmissivityPresets[i].Value < 0.0f) {
                        _State.Settings.Lepton.EmissivityPresets[i].Value = 0.0f;
                    } else if (_State.Settings.Lepton.EmissivityPresets[i].Value > 100.0f) {
                        _State.Settings.Lepton.EmissivityPresets[i].Value = 100.0f;
                    }

                    strncpy(_State.Settings.Lepton.EmissivityPresets[i].Description, name->valuestring,
                            sizeof(_State.Settings.Lepton.EmissivityPresets[i].Description));
                } else {
                    _State.Settings.Lepton.EmissivityPresets[i].Value = 100.0f;
                    strncpy(_State.Settings.Lepton.EmissivityPresets[i].Description, "Unknown",
                            sizeof(_State.Settings.Lepton.EmissivityPresets[i].Description));
                }

                ESP_LOGI(TAG, "  Preset %d: %s = %.2f", i, name->valuestring, value->valuedouble);
            }
        }
    }

    cJSON_Delete(json);

    /* Mark config as loaded */
    Error = nvs_set_u8(_State.NVS_Handle, "config_loaded", true);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_loaded flag: %d!", Error);
        return Error;
    }

    Error = nvs_commit(_State.NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config_loaded flag: %d!", Error);
        return Error;
    }

    ESP_LOGI(TAG, "Default config loaded and marked as valid");
    
    return ESP_OK;
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

    /* Load the settings from the NVS */
    Error = SettingsManager_Load(&_State.Settings);
    if (Error != ESP_OK) {
        ESP_LOGI(TAG, "No settings found, using factory defaults");

        /* Try to load default settings from JSON first (on first boot) */
        if (SettingsManager_LoadDefaultsFromJSON() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load default settings from JSON, using built-in defaults");

            /* Use built-in defaults */
            SettingsManager_InitDefaults(&_State.Settings);
        }

        /* Save the default settings to NVS */
        SettingsManager_Save(&_State.Settings);

        /* Load the JSON presets into the settings structure */
        SettingsManager_Load(&_State.Settings);
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

        ESP_LOGW(TAG, "Settings migration not implemented, using defaults");
        xSemaphoreGive(_State.Mutex);

        return ESP_FAIL;
    }

    xSemaphoreGive(_State.Mutex);

    ESP_LOGI(TAG, "Settings loaded from NVS");

    // TODO: Remove me
    return ESP_FAIL;
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

    /* Reset config_loaded flag to allow reloading default config */
    Error = nvs_set_u8(_State.NVS_Handle, "config_loaded", false);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_loaded flag: %d!", Error);
        return Error;
    }

    xSemaphoreGive(_State.Mutex);

    /* Reboot the ESP to allow reloading the settings config */
    esp_restart();

    /* Never reached */
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
