/*
 * application.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Application header file.
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
 */

#ifndef APPLICATION_H_
#define APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "Manager/managers.h"

#include <sdkconfig.h>

ESP_EVENT_DECLARE_BASE(LEPTON_EVENTS);
ESP_EVENT_DECLARE_BASE(DEVICE_EVENTS);
ESP_EVENT_DECLARE_BASE(GUI_EVENTS);
ESP_EVENT_DECLARE_BASE(SD_EVENTS);

/** @brief Lepton camera event identifiers.
 */
enum {
    LEPTON_EVENT_CAMERA_READY,                  /**< Lepton camera is ready. */
    LEPTON_EVENT_CAMERA_ERROR,                  /**< Lepton camera error occurred.
                                                     Data is transmitted in a App_Lepton_Device_t structure. */
    LEPTON_EVENT_RESPONSE_FPA_AUX_TEMP,         /**< FPA and AUX temperatures are ready.
                                                     Data is transmitted in a App_Lepton_Temperatures_t structure. */
    LEPTON_EVENT_RESPONSE_SPOTMETER,            /**< Spotmeter data is ready.
                                                     Data is transmitted in a App_Lepton_Spotmeter_t structure. */
    LEPTON_EVENT_RESPONSE_UPTIME,               /**< Uptime data is ready.
                                                     Data is transmitted as a uint32_t representing uptime in milliseconds. */
    LEPTON_EVENT_RESPONSE_PIXEL_TEMPERATURE,    /**< Pixel temperature data is ready.
                                                     Data is transmitted as a float. */
};

/** @brief Device status event identifiers.
 */
enum {
    DEVICE_EVENT_RESPONSE_BATTERY_VOLTAGE,      /**< New battery voltage reading available.
                                                     Data is transmitted in a App_Devices_Battery_t structure. */
    DEVICE_EVENT_RESPONSE_CHARGING,             /**< Charging state changed.
                                                     Data is transmitted as a bool. */
    DEVICE_EVENT_RESPONSE_TIME,                 /**< Device RTC time has been updated.
                                                     Data is transmitted in a struct tm structure. */
};

/** @brief GUI event identifiers.
 */
enum {
    GUI_EVENT_INIT_DONE,                        /**< GUI task initialization done. */
    GUI_EVENT_REQUEST_ROI,                      /**< Update the ROI rectangle on the GUI. */
    GUI_EVENT_REQUEST_FPA_AUX_TEMP,             /**< Request update of the FPA and AUX temperature. */
    GUI_EVENT_REQUEST_UPTIME,                   /**< Request update of the uptime. */
    GUI_EVENT_UPDATE_INFO,                      /**< Update the information screen. */
    GUI_EVENT_REQUEST_PIXEL_TEMPERATURE,        /**< Request update of pixel temperature.
                                                     Data is transmitted in a App_GUI_Screenposition_t structure. */
    GUI_EVENT_REQUEST_SPOTMETER,                /**< Request update of spotmeter data. */
};

/** @brief Structure representing a screen position.
 */
typedef struct {
    int16_t x;                                  /**< X coordinate. */
    int16_t y;                                  /**< Y coordinate. */
    int32_t Width;                              /**< Width of the screen element where the position is related to. */
    int32_t Height;                             /**< Height of the screen element where the position is related to. */
} App_GUI_Screenposition_t;

/** @brief Structure representing battery information.
 */
typedef struct {
    int Voltage;                                /**< Battery voltage in millivolts. */
    int Percentage;                             /**< Battery percentage (0-100%). */
} App_Devices_Battery_t;

/** @brief Structure representing a ready frame from the Lepton camera.
 */
typedef struct {
    uint8_t *Buffer;
    uint32_t Width;
    uint32_t Height;
    uint32_t Channels;
} App_Lepton_FrameReady_t;

/** @brief Structure representing FPA and AUX temperature from the Lepton camera.
 */
typedef struct {
    float FPA;                                  /**< Focal Plane Array temperature in Degree Celsius. */
    float AUX;                                  /**< Auxiliary temperature in Degree Celsius. */
} App_Lepton_Temperatures_t;

/** @brief Structure representing the Lepton camera device status.
 */
typedef struct {
    char PartNumber[33];                        /**< Lepton device part number. */
    char SerialNumber[24];                      /**< Lepton device serial number formatted as "XXXX-XXXX-XXXX-XXXX". */
} App_Lepton_Device_t;

/** @brief Structure representing the spotmeter results from the Lepton camera.
 */
typedef struct {
    float Max;                                  /**< Maximum temperature value in Degree Celsius within the spotmeter ROI. */
    float Min;                                  /**< Minimum temperature value in Degree Celsius within the spotmeter ROI. */
    float AverageTemperature;                   /**< Average temperature value in Degree Celsius within the spotmeter ROI. */
} App_Lepton_Spotmeter_t;

/** @brief Application context aggregating shared resources.
 */
typedef struct {
    QueueHandle_t Lepton_FrameEventQueue;
    Network_Config_t Network_Config;
    Server_Config_t Server_Config;
    App_Settings_t Settings;
} App_Context_t;

#endif /* APPLICATION_H_ */