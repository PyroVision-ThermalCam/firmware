/*
 * settings_types.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Settings Manager event types and definitions.
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

#ifndef SETTINGS_TYPES_H_
#define SETTINGS_TYPES_H_

#include <esp_event.h>

/** @brief Settings Manager events base.
 */
ESP_EVENT_DECLARE_BASE(SETTINGS_EVENTS);

/** @brief Settings event identifiers.
 */
enum {
    SETTINGS_EVENT_LOADED,                      /**< Settings loaded from NVS. */
    SETTINGS_EVENT_SAVED,                       /**< Settings saved to NVS. */
    SETTINGS_EVENT_CHANGED,                     /**< Settings changed. Data contains App_Settings_t structure. */
    SETTINGS_EVENT_LEPTON_CHANGED,              /**< Lepton settings changed.
                                                     Data contains App_Settings_Lepton_t. */
    SETTINGS_EVENT_WIFI_CHANGED,                /**< WiFi settings changed.
                                                     Data contains App_Settings_WiFi_t. */
    SETTINGS_EVENT_DISPLAY_CHANGED,             /**< Display settings changed.
                                                     Data contains App_Settings_Display_t. */
    SETTINGS_EVENT_SYSTEM_CHANGED,              /**< System settings changed.
                                                     Data contains App_Settings_System_t. */
    SETTINGS_EVENT_REQUEST_GET,                 /**< Request to get current settings. */
    SETTINGS_EVENT_REQUEST_SAVE,                /**< Request to save settings to NVS. */
    SETTINGS_EVENT_REQUEST_RESET,               /**< Request to reset settings to factory defaults. */
};

/** @brief Region of Interest (ROI) rectangle definition.
 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} App_Settings_ROI_t;

/** @brief Lepton camera settings.
 */
typedef struct {
    App_Settings_ROI_t SpotmeterROI;            /**< Spotmeter Region of Interest. */
    uint8_t Reserved[100];                      /**< Reserved for future use. */
} App_Settings_Lepton_t;

/** @brief WiFi settings.
 */
typedef struct {
    char SSID[32];                              /**< WiFi SSID. */
    char Password[64];                          /**< WiFi password. */
    bool AP_Mode;                               /**< Access Point mode (true) or Station mode (false). */
    char AP_SSID[32];                           /**< AP mode SSID. */
    char AP_Password[64];                       /**< AP mode password. */
    uint8_t Reserved[100];                      /**< Reserved for future use. */
} App_Settings_WiFi_t;

/** @brief Display settings.
 */
typedef struct {
    uint8_t Brightness;                         /**< Display brightness (0-100%). */
    uint16_t ScreenTimeout;                     /**< Screen timeout in seconds (0=never). */
    uint8_t Reserved[100];                      /**< Reserved for future use. */
} App_Settings_Display_t;

/** @brief System settings.
 */
typedef struct {
    char DeviceName[32];                        /**< Device name. */
    bool SDCard_AutoMount;                      /**< Automatically mount SD card. */
    bool Bluetooth_Enabled;                     /**< Bluetooth enabled. */
    char Timezone[32];                          /**< Timezone string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3"). */
    uint8_t Reserved[100];                      /**< Reserved for future use. */
} App_Settings_System_t;

/** @brief Complete application settings structure.
 */
typedef struct {
    uint32_t Version;                           /**< Settings version for migration. */
    App_Settings_Lepton_t Lepton;               /**< Lepton camera settings. */
    App_Settings_WiFi_t WiFi;                   /**< WiFi settings. */
    App_Settings_Display_t Display;             /**< Display settings. */
    App_Settings_System_t System;               /**< System settings. */
} App_Settings_t;

#endif /* SETTINGS_TYPES_H_ */
