/*
 * settingsManager.h
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

#ifndef SETTINGS_MANAGER_H_
#define SETTINGS_MANAGER_H_

#include <esp_err.h>
#include <stdbool.h>

#include "settings_types.h"

/** @brief Settings storage namespace in NVS.
 */
#define SETTINGS_NVS_NAMESPACE      "pyrovision"

/** @brief Current settings version for migration support.
 */
#define SETTINGS_VERSION            1

/** @brief  Initialize the Settings Manager.
 *  @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_Init(void);

/** @brief  Deinitialize the Settings Manager.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_Deinit(void);

/** @brief              Load all settings from NVS.
 *                      If settings don't exist, factory defaults are used.
 *  @param p_Settings   Pointer to settings structure to populate
 *  @return             ESP_OK on success, ESP_ERR_NVS_NOT_FOUND if no settings exist
 */
esp_err_t SettingsManager_Load(App_Settings_t *p_Settings);

/** @brief             Save all settings to NVS.
 *  @param p_Settings  Pointer to settings structure to save
 *  @return            ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t SettingsManager_Save(const App_Settings_t *p_Settings);

/** @brief              Get current settings (cached in RAM).
 *  @param p_Settings   Pointer to settings structure to populate
 *  @return             ESP_OK on success
 */
esp_err_t SettingsManager_Get(App_Settings_t *p_Settings);

/** @brief          Update Lepton settings.
 *                  Changes are cached and broadcasted via SETTINGS_EVENT_LEPTON_CHANGED.
 *  @param p_Lepton Pointer to Lepton settings
 *  @return         ESP_OK on success
 */
esp_err_t SettingsManager_SetLepton(const App_Settings_Lepton_t *p_Lepton);

/** @brief          Update WiFi settings.
 *                  Changes are cached and broadcasted via SETTINGS_EVENT_WIFI_CHANGED.
 *  @param p_WiFi   Pointer to WiFi settings
 *  @return         ESP_OK on success
 */
esp_err_t SettingsManager_SetWiFi(const App_Settings_WiFi_t *p_WiFi);

/** @brief              Update Display settings.
 *                      Changes are cached and broadcasted via SETTINGS_EVENT_DISPLAY_CHANGED.
 *  @param p_Display    Pointer to Display settings
 *  @return             ESP_OK on success
 */
esp_err_t SettingsManager_SetDisplay(const App_Settings_Display_t *p_Display);

/** @brief          Update System settings.
 *                  Changes are cached and broadcasted via SETTINGS_EVENT_SYSTEM_CHANGED.
 *  @param p_System Pointer to System settings
 *  @return         ESP_OK on success
 */
esp_err_t SettingsManager_SetSystem(const App_Settings_System_t *p_System);

/** @brief  Reset all settings to factory defaults.
 *          Erases NVS partition and reloads defaults.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_ResetToDefaults(void);

/** @brief              Get factory default settings.
 *  @param p_Settings   Pointer to settings structure to populate with defaults
 *  @return             ESP_OK on success
 */
esp_err_t SettingsManager_GetDefaults(App_Settings_t *p_Settings);

/** @brief  Commit cached settings to NVS storage.
 *          Should be called periodically or on shutdown to persist changes.
 *  @return ESP_OK on success
 */
esp_err_t SettingsManager_Commit(void);

/** @brief  Check if settings have unsaved changes.
 *  @return true if changes are pending, false otherwise
 */
bool SettingsManager_HasPendingChanges(void);

#endif /* SETTINGS_MANAGER_H_ */
