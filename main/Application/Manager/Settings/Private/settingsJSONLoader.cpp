/*
 * settingsJSONLoader.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: JSON settings loader for factory defaults.
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
#include <esp_littlefs.h>

#include <string.h>
#include <sys/stat.h>
#include <cJSON.h>

#include "settingsLoader.h"
#include "../settingsManager.h"

static const char *TAG = "settings_json_loader";

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadLepton(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *lepton = NULL;
    cJSON *emissivity_array = NULL;
    cJSON *roi_array = NULL;

    lepton = cJSON_GetObjectItem(p_JSON, "lepton");
    if (lepton != NULL) {
        emissivity_array = cJSON_GetObjectItem(lepton, "emissivity");
        if (cJSON_IsArray(emissivity_array)) {
            p_State->Settings.Lepton.EmissivityCount = cJSON_GetArraySize(emissivity_array);

            ESP_LOGD(TAG, "Found %d emissivity presets in JSON", p_State->Settings.Lepton.EmissivityCount);

            for (uint32_t i = 0; i < p_State->Settings.Lepton.EmissivityCount; i++) {
                cJSON *preset = cJSON_GetArrayItem(emissivity_array, i);
                cJSON *name = cJSON_GetObjectItem(preset, "name");
                cJSON *value = cJSON_GetObjectItem(preset, "value");

                if (cJSON_IsString(name) && cJSON_IsNumber(value)) {
                    p_State->Settings.Lepton.EmissivityPresets[i].Value = (float)(value->valuedouble);

                    /* Cap the emissivity value between 0 and 100 */
                    if (p_State->Settings.Lepton.EmissivityPresets[i].Value < 0.0f) {
                        p_State->Settings.Lepton.EmissivityPresets[i].Value = 0.0f;
                    } else if (p_State->Settings.Lepton.EmissivityPresets[i].Value > 100.0f) {
                        p_State->Settings.Lepton.EmissivityPresets[i].Value = 100.0f;
                    }

                    memset(p_State->Settings.Lepton.EmissivityPresets[i].Description, 0,
                           sizeof(p_State->Settings.Lepton.EmissivityPresets[i].Description));
                    strncpy(p_State->Settings.Lepton.EmissivityPresets[i].Description, name->valuestring,
                            sizeof(p_State->Settings.Lepton.EmissivityPresets[i].Description));

                    ESP_LOGD(TAG, "  Preset %d: %s = %.2f", i, name->valuestring, value->valuedouble);
                }
            }
        } else {
            SettingsManager_InitDefaultLeptonEmissivityPresets(&p_State->Settings);
        }

        roi_array = cJSON_GetObjectItem(lepton, "roi");
        if (cJSON_IsArray(roi_array)) {
        } else {
            SettingsManager_InitDefaultLeptonROIs(&p_State->Settings);
        }
    } else {
        SettingsManager_InitDefaultLepton(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadDisplay(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *display = NULL;

    display = cJSON_GetObjectItem(p_JSON, "display");
    if (display != NULL) {
        cJSON *brightness = cJSON_GetObjectItem(display, "brightness");
        if (cJSON_IsNumber(brightness)) {
            p_State->Settings.Display.Brightness = (uint8_t)(brightness->valueint);
        }

        cJSON *timeout = cJSON_GetObjectItem(display, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.Display.Timeout = (uint16_t)(timeout->valueint);
        }
    } else {
        SettingsManager_InitDefaultDisplay(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadWiFi(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *wifi = NULL;

    wifi = cJSON_GetObjectItem(p_JSON, "wifi");
    if (wifi != NULL) {
        cJSON *maxRetries = cJSON_GetObjectItem(wifi, "maxRetries");
        if (cJSON_IsNumber(maxRetries)) {
            p_State->Settings.WiFi.MaxRetries = (uint8_t)(maxRetries->valueint);
        }

        cJSON *retryInterval = cJSON_GetObjectItem(wifi, "retryInterval");
        if (cJSON_IsNumber(retryInterval)) {
            p_State->Settings.WiFi.RetryInterval = (uint32_t)(retryInterval->valueint);
        }

        cJSON *autoConnect = cJSON_GetObjectItem(wifi, "autoConnect");
        if (cJSON_IsBool(autoConnect)) {
            p_State->Settings.WiFi.AutoConnect = cJSON_IsTrue(autoConnect);
        }

        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        if (cJSON_IsString(ssid)) {
            strncpy(p_State->Settings.WiFi.SSID, ssid->valuestring, sizeof(p_State->Settings.WiFi.SSID));
        }

        cJSON *password = cJSON_GetObjectItem(wifi, "password");
        if (cJSON_IsString(password)) {
            strncpy(p_State->Settings.WiFi.Password, password->valuestring, sizeof(p_State->Settings.WiFi.Password));
        }
    } else {
        SettingsManager_InitDefaultWiFi(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadProvisioning(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *provisioning = NULL;

    provisioning = cJSON_GetObjectItem(p_JSON, "provisioning");
    if (provisioning != NULL) {
        cJSON *name = cJSON_GetObjectItem(provisioning, "name");
        if (cJSON_IsString(name)) {
            strncpy(p_State->Settings.Provisioning.Name, name->valuestring, sizeof(p_State->Settings.Provisioning.Name));
        }

        cJSON *pop = cJSON_GetObjectItem(provisioning, "pop");
        if (cJSON_IsString(pop)) {
            strncpy(p_State->Settings.Provisioning.PoP, pop->valuestring, sizeof(p_State->Settings.Provisioning.PoP));
        }

        cJSON *timeout = cJSON_GetObjectItem(provisioning, "timeout");
        if (cJSON_IsNumber(timeout)) {
            p_State->Settings.Provisioning.Timeout = (uint32_t)(timeout->valueint);
        }
    } else {
        SettingsManager_InitDefaultProvisioning(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadSystem(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *system = NULL;

    system = cJSON_GetObjectItem(p_JSON, "system");
    if (system != NULL) {
        cJSON *timezone = cJSON_GetObjectItem(system, "timezone");
        if (cJSON_IsString(timezone)) {
            strncpy(p_State->Settings.System.Timezone, timezone->valuestring, sizeof(p_State->Settings.System.Timezone));
        }
    } else {
        SettingsManager_InitDefaultSystem(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadHTTPServer(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *http_server = NULL;

    http_server = cJSON_GetObjectItem(p_JSON, "http-server");
    if (http_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(http_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.HTTPServer.Port = (uint16_t)(port->valueint);
        }

        cJSON *wsPingIntervalSec = cJSON_GetObjectItem(http_server, "wsPingIntervalSec");
        if (cJSON_IsNumber(wsPingIntervalSec)) {
            p_State->Settings.HTTPServer.WSPingIntervalSec = (uint16_t)(wsPingIntervalSec->valueint);
        }

        cJSON *maxClients = cJSON_GetObjectItem(http_server, "maxClients");
        if (cJSON_IsNumber(maxClients)) {
            p_State->Settings.HTTPServer.MaxClients = (uint8_t)(maxClients->valueint);
        }
    } else {
        SettingsManager_InitDefaultHTTPServer(&p_State->Settings);
    }
}

/** @brief          
 *  @param p_State  
 *  @param p_JSON   
 */
static void SettingsManager_LoadVISAServer(SettingsManager_State_t *p_State, const cJSON *p_JSON)
{
    cJSON *visa_server = NULL;

    visa_server = cJSON_GetObjectItem(p_JSON, "visa-server");
    if (visa_server != NULL) {
        cJSON *port = cJSON_GetObjectItem(visa_server, "port");
        if (cJSON_IsNumber(port)) {
            p_State->Settings.VISAServer.Port = (uint16_t)(port->valueint);
        }
    } else {
        SettingsManager_InitDefaultVISAServer(&p_State->Settings);
    }
}

esp_err_t SettingsManager_LoadDefaultsFromJSON(SettingsManager_State_t *p_State)
{
    uint8_t ConfigLoaded = 0;
    esp_err_t Error;
    cJSON *json = NULL;
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
      .read_only = true,
      .dont_mount = false
    };

    Error = nvs_get_u8(p_State->NVS_Handle, "config_loaded", &ConfigLoaded);
    if ((Error == ESP_OK) && (ConfigLoaded == true)) {
        ESP_LOGD(TAG, "Default config already loaded, skipping");
        //return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing LittleFS");

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
        ESP_LOGD(TAG, "Partition size: total: %zu, used: %zu", Total, Used);
    }

    ESP_LOGD(TAG, "Opening settings file...");
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

    ESP_LOGD(TAG, "File size: %ld bytes", FileSize);

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
        esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

        return ESP_FAIL;
    }

    esp_vfs_littlefs_unregister(LittleFS_Config.partition_label);

    ESP_LOGD(TAG, "File read successfully, parsing JSON...");

    /* Parse JSON */
    json = cJSON_Parse(SettingsBuffer);
    heap_caps_free(SettingsBuffer);
    SettingsBuffer = NULL;

    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON Parse Error: %s!", error_ptr);
        } else {
            ESP_LOGE(TAG, "JSON Parse Error: Unknown!");
        }

        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "JSON parsed successfully");

    /* Extract display settings */
    SettingsManager_LoadDisplay(p_State, json);

    /* Extract provisioning settings */
    SettingsManager_LoadProvisioning(p_State, json);

    /* Extract WiFi settings */
    SettingsManager_LoadWiFi(p_State, json);

    /* Extract system settings */
    SettingsManager_LoadSystem(p_State, json);

    /* Extract Lepton settings */
    SettingsManager_LoadLepton(p_State, json);

    /* Extract HTTP Server settings */
    SettingsManager_LoadHTTPServer(p_State, json);

    /* Extract VISA Server settings */
    SettingsManager_LoadVISAServer(p_State, json);

    cJSON_Delete(json);

    /* Mark config as loaded */
    Error = nvs_set_u8(p_State->NVS_Handle, "config_loaded", true);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config_loaded flag: %d!", Error);
        return Error;
    }

    Error = nvs_commit(p_State->NVS_Handle);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config_loaded flag: %d!", Error);
        return Error;
    }

    ESP_LOGD(TAG, "Default config loaded and marked as valid");
    
    return ESP_OK;
}