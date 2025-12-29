/*
 * guiTask.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: GUI task implementation.
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
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include <esp_efuse.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lvgl.h>

#include <cstring>
#include <time.h>

#include "guiTask.h"
#include "Export/ui.h"
#include "Application/application.h"
#include "Application/Manager/managers.h"
#include "Application/Manager/Network/Server/server.h"
#include "Private/guiHelper.h"

#include "lepton.h"

ESP_EVENT_DEFINE_BASE(GUI_EVENTS);

/* Touch calibration ranges (based on measurements)
 * Adjusted: RAW_X increased to shift touch point up
 */
const int16_t TOUCH_RAW_X_MIN = 47;
const int16_t TOUCH_RAW_X_MAX = 281;
const int16_t TOUCH_RAW_Y_MIN = 30;
const int16_t TOUCH_RAW_Y_MAX = 234;

static GUI_Task_State_t _GUITask_State;

static const char *TAG = "gui_task";

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Devices_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Devices event received: ID=%d", ID);

    switch (ID) {
        case DEVICE_EVENT_RESPONSE_BATTERY_VOLTAGE: {
            memcpy(&_GUITask_State.BatteryInfo, p_Data, sizeof(App_Devices_Battery_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, BATTERY_VOLTAGE_READY);

            break;
        }
        case DEVICE_EVENT_RESPONSE_CHARGING: {
            memcpy(&_GUITask_State.ChargeStatus, p_Data, sizeof(bool));

            xEventGroupSetBits(_GUITask_State.EventGroup, BATTERY_CHARGING_STATUS_READY);

            break;
        }
    }
}

/** @brief              Time event handler.
 *  @param p_HandlerArgs Handler argument
 *  @param Base         Event base
 *  @param ID           Event ID
 *  @param p_Data       Event-specific data
 */
static void on_Time_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Time event received: ID=%d", ID);

    switch (ID) {
        case TIME_EVENT_SYNCHRONIZED: {
            break;
        }
        case TIME_EVENT_SOURCE_CHANGED: {
            break;
        }
    }
}

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Network_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Network event received: ID=%d", ID);

    switch (ID) {
        case NETWORK_EVENT_WIFI_CONNECTED: {
            break;
        }
        case NETWORK_EVENT_WIFI_GOT_IP: {
            memcpy(&_GUITask_State.IP_Info, p_Data, sizeof(Network_IP_Info_t));
            _GUITask_State.WiFiConnected = true;

            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_WIFI_DISCONNECTED: {
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STARTED: {
            _GUITask_State.ProvisioningActive = true;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_STOPPED: {
            _GUITask_State.ProvisioningActive = false;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            /* Also trigger WiFi status update to show correct connection state */
            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_SUCCESS: {
            _GUITask_State.ProvisioningActive = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_PROV_TIMEOUT: {
            _GUITask_State.ProvisioningActive = false;
            _GUITask_State.WiFiConnected = false;

            xEventGroupSetBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);

            /* Also trigger WiFi status update to show correct connection state */
            xEventGroupSetBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);

            break;
        }
        case NETWORK_EVENT_SERVER_STARTED: {
            /* Register thermal frame with server (called after server is started) */
            Server_SetThermalFrame(&_GUITask_State.NetworkFrame);
            ESP_LOGD(TAG, "Network frame registered with server");

            break;
        }
        case NETWORK_EVENT_AP_STA_CONNECTED: {
            break;
        }
        case NETWORK_EVENT_AP_STA_DISCONNECTED: {
            break;
        }
    }
}

/** @brief                  SD card event handler.
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_SD_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "SD card event received: ID=%ld", ID);

    switch (ID) {
        case SD_EVENT_CARD_CHANGED: {
            _GUITask_State.CardPresent = *(bool *)p_Data;
            ESP_LOGI(TAG, "SD card %s", (_GUITask_State.CardPresent) ? "inserted" : "removed");

            xEventGroupSetBits(_GUITask_State.EventGroup, SD_CARD_STATE_CHANGED);

            break;
        }
        case SD_EVENT_MOUNTED: {
            ESP_LOGI(TAG, "SD card mounted successfully event");

            xEventGroupSetBits(_GUITask_State.EventGroup, SD_CARD_MOUNTED);

            break;
        }
        case SD_EVENT_MOUNT_ERROR: {
            ESP_LOGE(TAG, "SD card mount error event");

            xEventGroupSetBits(_GUITask_State.EventGroup, SD_CARD_MOUNT_ERROR);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unknown SD event ID: %ld", ID);
            break;
        }
    }
}

/** @brief
 *  @param p_HandlerArgs    Handler argument
 *  @param Base             Event base
 *  @param ID               Event ID
 *  @param p_Data           Event-specific data
 */
static void on_Lepton_Event_Handler(void *p_HandlerArgs, esp_event_base_t Base, int32_t ID, void *p_Data)
{
    ESP_LOGD(TAG, "Lepton event received: ID=%d", ID);

    switch (ID) {
        case LEPTON_EVENT_CAMERA_READY: {
            memcpy(&_GUITask_State.LeptonDeviceInfo, ((App_Lepton_Device_t *)p_Data), sizeof(App_Lepton_Device_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_CAMERA_READY);

            break;
        }
        case LEPTON_EVENT_CAMERA_ERROR: {
            break;
        }
        case LEPTON_EVENT_RESPONSE_FPA_AUX_TEMP: {
            memcpy(&_GUITask_State.LeptonTemperatures, p_Data, sizeof(App_Lepton_Temperatures_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_TEMP_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_SPOTMETER: {
            memcpy(&_GUITask_State.SpotmeterInfo, p_Data, sizeof(App_Lepton_Spotmeter_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_SPOTMETER_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_UPTIME: {
            memcpy(&_GUITask_State.LeptonUptime, p_Data, sizeof(uint32_t));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_UPTIME_READY);

            break;
        }
        case LEPTON_EVENT_RESPONSE_PIXEL_TEMPERATURE: {
            memcpy(&_GUITask_State.SpotTemperature, p_Data, sizeof(float));

            xEventGroupSetBits(_GUITask_State.EventGroup, LEPTON_PIXEL_TEMPERATURE_READY);

            break;
        }
        default: {
            ESP_LOGW(TAG, "Unhandled Lepton event ID: %d", ID);
            break;
        }
    }
}

/** @brief Update the information screen labels.
 */
static void GUI_Update_Info(void)
{
    uint8_t mac[6];
    char mac_str[19];

    esp_efuse_mac_get_default(mac);
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    lv_label_set_text(ui_Label_Info_Lepton_Serial, _GUITask_State.LeptonDeviceInfo.SerialNumber);
    lv_label_set_text(ui_Label_Info_Lepton_Part, _GUITask_State.LeptonDeviceInfo.PartNumber);
    lv_label_set_text(ui_Label_Info_MAC, mac_str);
}

/** @brief      Update the ROI rectangle on the GUI and on the Lepton.
 *  @param x    X position of the ROI (in Lepton coordinates 0-159)
 *  @param y    Y position (in Lepton coordinates 0-119)
 *  @param w    Width of the ROI
 *  @param h    Height of the ROI
 */
static void GUI_Update_ROI(int32_t x, int32_t y, int32_t w, int32_t h)
{
    App_Settings_ROI_t ROI;

    /* Clamp values to Lepton sensor dimensions */
    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    if (x + w > 160) {
        w = 160 - x;
    }

    if (y + h > 120) {
        h = 120 - y;
    }

    /* Minimum size constraints */
    if (w < 10) {
        w = 10;
    } else if (h < 10) {
        h = 10;
    }

    ROI = {
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .w = (uint16_t)w,
        .h = (uint16_t)h
    };

    /* Update visual rectangle on display (convert Lepton coords to display coords) */
    if (ui_Image_Main_Thermal_ROI != NULL) {
        int32_t display_width;
        int32_t display_height;

        ESP_LOGD(TAG, "Updating ROI rectangle - Start: (%ld,%ld), End: (%ld,%ld), Size: %ldx%ld",
                 ROI.x, ROI.y, ROI.x + ROI.w, ROI.y + ROI.h, ROI.w, ROI.h);

        display_width = lv_obj_get_width(ui_Image_Thermal);
        display_height = lv_obj_get_height(ui_Image_Thermal);

        int32_t disp_x = (x * display_width) / 160;
        int32_t disp_y = (y * display_height) / 120;
        int32_t disp_w = (w * display_width) / 160;
        int32_t disp_h = (h * display_height) / 120;

        /* Remove alignment to allow manual positioning */
        lv_obj_set_align(ui_Image_Main_Thermal_ROI, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(ui_Image_Main_Thermal_ROI, disp_x, disp_y);
        lv_obj_set_size(ui_Image_Main_Thermal_ROI, disp_w, disp_h);
    }

    /* Send ROI update to Lepton task */
    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_ROI, &ROI, sizeof(App_Settings_ROI_t), portMAX_DELAY);
}

/** @brief  Event handler for ROI reset button.
 *          Handles both LONG_PRESSED (reset) and CLICKED (toggle visibility).
 */
static void ROI_Clicked_Event_Handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_LONG_PRESSED) {
        ESP_LOGD(TAG, "ROI reset - long press detected");

        /* Set flag to prevent subsequent CLICKED event */
        _GUITask_State.ROI_LongPressActive = true;

        /* Reset ROI to center with default size */
        GUI_Update_ROI(60, 40, 40, 40);
    } else if (code == LV_EVENT_CLICKED) {
        /* Check if this click came after a long press */
        if (_GUITask_State.ROI_LongPressActive) {
            ESP_LOGD(TAG, "ROI click suppressed after long press");
            _GUITask_State.ROI_LongPressActive = false;
            return;
        }

        ESP_LOGD(TAG, "ROI toggle visibility");
        /* Toggle ROI visibility on normal click */
        if (lv_obj_has_flag(ui_Image_Main_Thermal_ROI, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(ui_Image_Main_Thermal_ROI, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui_Image_Main_Thermal_ROI, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/** @brief  Event handler for ROI rectangle touch interactions.
 *          - Normal drag at edges: Immediate resize
 *          - Long press inside center: Activate move mode
 */
static void ROI_Rectangle_Event_Handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t point;
        lv_area_t roi_area;

        lv_indev_get_point(indev, &point);

        /* Store initial touch position and ROI state */
        _GUITask_State.ROI_DragStartX = point.x;
        _GUITask_State.ROI_DragStartY = point.y;
        _GUITask_State.ROI_InitialX = lv_obj_get_x(obj);
        _GUITask_State.ROI_InitialY = lv_obj_get_y(obj);
        _GUITask_State.ROI_InitialW = lv_obj_get_width(obj);
        _GUITask_State.ROI_InitialH = lv_obj_get_height(obj);

        /* Get absolute position of ROI on screen */
        lv_obj_get_coords(obj, &roi_area);

        /* Calculate touch position relative to ROI (not screen) */
        int32_t rel_x = point.x - roi_area.x1;
        int32_t rel_y = point.y - roi_area.y1;
        int32_t edge_threshold = 20; /* pixels from edge to trigger resize */

        bool near_left = rel_x < edge_threshold;
        bool near_right = rel_x > _GUITask_State.ROI_InitialW - edge_threshold;
        bool near_top = rel_y < edge_threshold;
        bool near_bottom = rel_y > _GUITask_State.ROI_InitialH - edge_threshold;

        /* Determine resize mode - allow combining edges (e.g., corner resize) */
        _GUITask_State.ROI_ResizeMode = 0;
        if (near_left) {
            _GUITask_State.ROI_ResizeMode |= 1;  /* Left edge */
        } else if (near_right) {
            _GUITask_State.ROI_ResizeMode |= 2;  /* Right edge */
        } else if (near_top) {
            _GUITask_State.ROI_ResizeMode |= 4;  /* Top edge */
        } else if (near_bottom) {
            _GUITask_State.ROI_ResizeMode |= 8;  /* Bottom edge */
        }

        /* If touch is at edge/corner, activate resize immediately */
        if (_GUITask_State.ROI_ResizeMode != 0) {
            _GUITask_State.ROI_EditMode = true;
            ESP_LOGD(TAG, "ROI RESIZE activated - mode=%d (1=left, 2=right, 4=top, 8=bottom) rel_pos=(%ld,%ld)",
                     _GUITask_State.ROI_ResizeMode, rel_x, rel_y);
        } else {
            /* Touch is in center - wait for long press to activate move mode */
            _GUITask_State.ROI_EditMode = false;
            ESP_LOGD(TAG, "ROI center pressed - waiting for long press to activate MOVE mode");
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        /* Long press detected - only activate move mode if touch was in center (ResizeMode == 0) */
        if (_GUITask_State.ROI_ResizeMode == 0) {
            _GUITask_State.ROI_EditMode = true;
            ESP_LOGD(TAG, "ROI MOVE mode ACTIVATED after long press");
        }
    } else if (code == LV_EVENT_PRESSING && _GUITask_State.ROI_EditMode) {
        lv_indev_t *indev = lv_indev_get_act();
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        /* Calculate drag delta */
        int32_t delta_x = point.x - _GUITask_State.ROI_DragStartX;
        int32_t delta_y = point.y - _GUITask_State.ROI_DragStartY;

        int32_t img_width = lv_obj_get_width(ui_Image_Thermal);
        int32_t img_height = lv_obj_get_height(ui_Image_Thermal);
        int32_t min_size = 15; /* Minimum ROI size in display pixels */

        if (_GUITask_State.ROI_ResizeMode == 0) {
            /* Move mode */
            int32_t new_x = _GUITask_State.ROI_InitialX + delta_x;
            int32_t new_y = _GUITask_State.ROI_InitialY + delta_y;

            if (new_x < 0) {
                new_x = 0;
            } else if (new_y < 0) {
                new_y = 0;
            } else if (new_x + _GUITask_State.ROI_InitialW > img_width) {
                new_x = img_width - _GUITask_State.ROI_InitialW;
            } else if (new_y + _GUITask_State.ROI_InitialH > img_height) {
                new_y = img_height - _GUITask_State.ROI_InitialH;
            }

            lv_obj_set_pos(obj, new_x, new_y);
        } else {
            /* Resize mode */
            int32_t new_x = _GUITask_State.ROI_InitialX;
            int32_t new_y = _GUITask_State.ROI_InitialY;
            int32_t new_w = _GUITask_State.ROI_InitialW;
            int32_t new_h = _GUITask_State.ROI_InitialH;

            /* Resize left edge */
            if (_GUITask_State.ROI_ResizeMode & 1) {
                new_x = _GUITask_State.ROI_InitialX + delta_x;
                new_w = _GUITask_State.ROI_InitialW - delta_x;
                if (new_x < 0) {
                    new_w += new_x;
                    new_x = 0;
                } else if (new_w < min_size) {
                    new_x = _GUITask_State.ROI_InitialX + _GUITask_State.ROI_InitialW - min_size;
                    new_w = min_size;
                }
            }

            /* Resize right edge */
            if (_GUITask_State.ROI_ResizeMode & 2) {
                new_w = _GUITask_State.ROI_InitialW + delta_x;
                if (new_x + new_w > img_width) {
                    new_w = img_width - new_x;
                } else if (new_w < min_size) {
                    new_w = min_size;
                }
            }

            /* Resize top edge */
            if (_GUITask_State.ROI_ResizeMode & 4) {
                new_y = _GUITask_State.ROI_InitialY + delta_y;
                new_h = _GUITask_State.ROI_InitialH - delta_y;
                if (new_y < 0) {
                    new_h += new_y;
                    new_y = 0;
                } else if (new_h < min_size) {
                    new_y = _GUITask_State.ROI_InitialY + _GUITask_State.ROI_InitialH - min_size;
                    new_h = min_size;
                }
            }

            /* Resize bottom edge */
            if (_GUITask_State.ROI_ResizeMode & 8) {
                new_h = _GUITask_State.ROI_InitialH + delta_y;
                if (new_y + new_h > img_height) {
                    new_h = img_height - new_y;
                } else if (new_h < min_size) {
                    new_h = min_size;
                }
            }

            lv_obj_set_pos(obj, new_x, new_y);
            lv_obj_set_size(obj, new_w, new_h);
        }
    } else if (code == LV_EVENT_RELEASED) {
        /* Only process if edit mode was active (after long press) */
        if (_GUITask_State.ROI_EditMode) {
            _GUITask_State.ROI_EditMode = false;

            /* Convert display coordinates back to Lepton coordinates */
            int32_t disp_x = lv_obj_get_x(obj);
            int32_t disp_y = lv_obj_get_y(obj);
            int32_t disp_w = lv_obj_get_width(obj);
            int32_t disp_h = lv_obj_get_height(obj);

            int32_t img_width = lv_obj_get_width(ui_Image_Thermal);
            int32_t img_height = lv_obj_get_height(ui_Image_Thermal);

            int32_t lepton_x = (disp_x * 160) / img_width;
            int32_t lepton_y = (disp_y * 120) / img_height;
            int32_t lepton_w = (disp_w * 160) / img_width;
            int32_t lepton_h = (disp_h * 120) / img_height;

            /* Update ROI on Lepton camera */
            GUI_Update_ROI(lepton_x, lepton_y, lepton_w, lepton_h);

            ESP_LOGD(TAG, "ROI edit mode DEACTIVATED - changes saved");
        } else {
            ESP_LOGD(TAG, "ROI released without edit mode (short press ignored)");
        }
    }
}

/** @brief Create temperature gradient canvas for palette visualization.
 *         Generates a vertical gradient from hot (top) to cold (bottom).
 */
static void UI_Canvas_AddTempGradient(void)
{
    /* Generate gradient pixel by pixel */
    uint16_t *buffer = (uint16_t *)_GUITask_State.GradientCanvasBuffer;

    for (uint32_t y = 0; y < _GUITask_State.GradientImageDescriptor.header.h; y++) {
        uint32_t index;

        /* Map y position to palette index (0 = top/hot = white, 179 = bottom/cold = black)
         * Iron palette: index 0 = black (cold), index 255 = white (hot)
         * So we need to invert: top (y=0) should be index 255, bottom (y=179) should be index 0
         */
        index = 255 - (y * 255 / (_GUITask_State.GradientImageDescriptor.header.h - 1));

        /* Get RGB888 values from palette */
        uint8_t r8 = Lepton_Palette_Iron[index][0];
        uint8_t g8 = Lepton_Palette_Iron[index][1];
        uint8_t b8 = Lepton_Palette_Iron[index][2];

        /* Convert RGB888 to RGB565 */
        uint16_t r5 = (r8 >> 3) & 0x1F;
        uint16_t g6 = (g8 >> 2) & 0x3F;
        uint16_t b5 = (b8 >> 3) & 0x1F;

        /* Fill entire row with same color */
        for (uint32_t x = 0; x < _GUITask_State.GradientImageDescriptor.header.w; x++) {
            buffer[y * _GUITask_State.GradientImageDescriptor.header.w + x] = (r5 << 11) | (g6 << 5) | b5;
        }
    }
}

/** @brief LVGL touch read callback for XPT2046 touch controller.
 */
static void XPT2046_LVGL_ReadCallback(lv_indev_t *p_Indev, lv_indev_data_t *p_Data)
{
    uint8_t Count = 0;
    esp_lcd_touch_point_data_t Data[1];
    esp_lcd_touch_handle_t Touch;
    esp_err_t Error;

    Touch = (esp_lcd_touch_handle_t)lv_indev_get_user_data(p_Indev);
    esp_lcd_touch_read_data(Touch);

    Error = esp_lcd_touch_get_data(Touch, Data, &Count, sizeof(Data) / sizeof(Data[0]));

    if ((Error == ESP_OK) && (Count > 0)) {
        uint16_t raw_x = Data[0].x;
        uint16_t raw_y = Data[0].y;

        /* Clamp raw values to calibrated range */
        int16_t clamped_x = (raw_x < TOUCH_RAW_X_MIN) ? TOUCH_RAW_X_MIN : (raw_x > TOUCH_RAW_X_MAX) ? TOUCH_RAW_X_MAX : raw_x;
        int16_t clamped_y = (raw_y < TOUCH_RAW_Y_MIN) ? TOUCH_RAW_Y_MIN : (raw_y > TOUCH_RAW_Y_MAX) ? TOUCH_RAW_Y_MAX : raw_y;

        /* Swap axes and scale: Display X from Raw Y, Display Y from Raw X */
        p_Data->point.x = (clamped_y - TOUCH_RAW_Y_MIN) * (CONFIG_GUI_WIDTH - 1) / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN);
        p_Data->point.y = (clamped_x - TOUCH_RAW_X_MIN) * (CONFIG_GUI_HEIGHT - 1) / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN);
        p_Data->state = LV_INDEV_STATE_PRESSED;

        ESP_LOGD(TAG, "Touch RAW: (%d, %d) -> MAPPED: (%d, %d)", raw_x, raw_y, p_Data->point.x, p_Data->point.y);

#ifdef CONFIG_GUI_TOUCH_DEBUG
        /* Update touch debug visualization */
        if (_GUITask_State.TouchDebugCircle != NULL) {
            lv_obj_set_pos(_GUITask_State.TouchDebugCircle, p_Data->point.x - 10, p_Data->point.y - 10);
            lv_obj_clear_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_HIDDEN);
        }

        if (_GUITask_State.TouchDebugLabel != NULL) {
            char buf[64];

            snprintf(buf, sizeof(buf), "Raw: %u,%u\nMap: %li,%li", raw_x, raw_y, p_Data->point.x, p_Data->point.y);
            lv_label_set_text(_GUITask_State.TouchDebugLabel, buf);
            lv_obj_clear_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    } else {
        p_Data->state = LV_INDEV_STATE_RELEASED;

#ifdef CONFIG_GUI_TOUCH_DEBUG
        /* Hide touch debug visualization when released */
        if (_GUITask_State.TouchDebugCircle != NULL) {
            lv_obj_add_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_HIDDEN);
        }

        if (_GUITask_State.TouchDebugLabel != NULL) {
            lv_obj_add_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_HIDDEN);
        }
#endif
    }
}

void Task_GUI(void *p_Parameters)
{
    App_Context_t *App_Context;

    esp_task_wdt_add(NULL);

    App_Context = reinterpret_cast<App_Context_t *>(p_Parameters);
    ESP_LOGD(TAG, "GUI Task started on core %d", xPortGetCoreID());

    /* Show splash screen first */
    /* Initialization process: */
    /*  - Loading the settings */
    /*  - Waiting for the Lepton */
    do {
        EventBits_t EventBits;

        esp_task_wdt_reset();

        EventBits = xEventGroupGetBits(_GUITask_State.EventGroup);
        if (EventBits & LEPTON_CAMERA_READY) {

            lv_bar_set_value(ui_SplashScreen_LoadingBar, 100, LV_ANIM_OFF);
            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_CAMERA_READY);
        }

        // TODO: Add timeout

        lv_timer_handler();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } while (lv_bar_get_value(ui_SplashScreen_LoadingBar) < lv_bar_get_max_value(ui_SplashScreen_LoadingBar));

    lv_disp_load_scr(ui_Main);

    /* Process layout changes after loading new screen */
    lv_timer_handler();

    /* Set the initial ROI FIRST to give it a size */
    GUI_Update_ROI(App_Context->Settings.Lepton.SpotmeterROI.x, App_Context->Settings.Lepton.SpotmeterROI.y,
                   App_Context->Settings.Lepton.SpotmeterROI.w, App_Context->Settings.Lepton.SpotmeterROI.h);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_RELEASED, NULL);

    GUI_Update_Info();

    _GUITask_State.RunTask = true;
    while (_GUITask_State.RunTask) {
        EventBits_t EventBits;
        App_Lepton_FrameReady_t LeptonEvent;

        esp_task_wdt_reset();

        /* Check for new thermal frame */
        if (xQueueReceive(App_Context->Lepton_FrameEventQueue, &LeptonEvent, 0) == pdTRUE) {
            uint8_t *dst;
            uint32_t Image_Width;
            uint32_t Image_Height;

            /* Scale from source (160x120) to destination (240x180) using bilinear interpolation */
            dst = _GUITask_State.ThermalCanvasBuffer;
            Image_Width = lv_obj_get_width(ui_Image_Thermal);
            Image_Height = lv_obj_get_height(ui_Image_Thermal);

            /* Skip if image widget not properly initialized yet */
            if ((Image_Width == 0) || (Image_Height == 0)) {
                ESP_LOGW(TAG, "Image widget not ready yet (size: %ux%u), skipping frame", Image_Width, Image_Height);
                continue;
            }

            /* Pre-calculate scaling factors (fixed-point 16.16) */
            uint32_t x_ratio = ((LeptonEvent.Width - 1) << 16) / Image_Width;
            uint32_t y_ratio = ((LeptonEvent.Height - 1) << 16) / Image_Height;

            for (uint32_t dst_y = 0; dst_y < Image_Height; dst_y++) {
                uint32_t src_y_fixed = dst_y * y_ratio;
                uint32_t y0 = src_y_fixed >> 16;
                uint32_t y1 = (y0 + 1 < LeptonEvent.Height) ? y0 + 1 : y0;
                uint32_t y_frac = (src_y_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                uint32_t y_inv = 256 - y_frac;

                for (uint32_t dst_x = 0; dst_x < Image_Width; dst_x++) {
                    uint32_t src_x_fixed = dst_x * x_ratio;
                    uint32_t x0 = src_x_fixed >> 16;
                    uint32_t x1 = (x0 + 1 < LeptonEvent.Width) ? x0 + 1 : x0;
                    uint32_t x_frac = (src_x_fixed >> 8) & 0xFF; /* 8-bit fractional part */
                    uint32_t x_inv = 256 - x_frac;

                    /* Get the four surrounding pixels */
                    uint32_t idx00 = (y0 * LeptonEvent.Width + x0) * 3;
                    uint32_t idx10 = (y0 * LeptonEvent.Width + x1) * 3;
                    uint32_t idx01 = (y1 * LeptonEvent.Width + x0) * 3;
                    uint32_t idx11 = (y1 * LeptonEvent.Width + x1) * 3;

                    /* Bilinear interpolation using fixed-point arithmetic (8.8 format) */
                    /* Weight: (256-x_frac)*(256-y_frac), x_frac*(256-y_frac), etc. */
                    uint32_t w00 = (x_inv * y_inv) >> 8;
                    uint32_t w10 = (x_frac * y_inv) >> 8;
                    uint32_t w01 = (x_inv * y_frac) >> 8;
                    uint32_t w11 = (x_frac * y_frac) >> 8;

                    uint32_t r = (LeptonEvent.Buffer[idx00 + 0] * w00 +
                                  LeptonEvent.Buffer[idx10 + 0] * w10 +
                                  LeptonEvent.Buffer[idx01 + 0] * w01 +
                                  LeptonEvent.Buffer[idx11 + 0] * w11) >> 8;

                    uint32_t g = (LeptonEvent.Buffer[idx00 + 1] * w00 +
                                  LeptonEvent.Buffer[idx10 + 1] * w10 +
                                  LeptonEvent.Buffer[idx01 + 1] * w01 +
                                  LeptonEvent.Buffer[idx11 + 1] * w11) >> 8;

                    uint32_t b_val = (LeptonEvent.Buffer[idx00 + 2] * w00 +
                                      LeptonEvent.Buffer[idx10 + 2] * w10 +
                                      LeptonEvent.Buffer[idx01 + 2] * w01 +
                                      LeptonEvent.Buffer[idx11 + 2] * w11) >> 8;

                    /* Destination pixel index (rotated 180 degrees) */
                    uint32_t rot_y = Image_Height - 1 - dst_y;
                    uint32_t rot_x = Image_Width - 1 - dst_x;
                    uint32_t dst_idx = rot_y * Image_Width + rot_x;

                    /* Convert to RGB565 - LVGL handles swapping with RGB565_SWAPPED */
                    uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b_val >> 3);

                    /* Low byte first */
                    dst[dst_idx * 2 + 0] = rgb565 & 0xFF;

                    /* High byte second */
                    dst[dst_idx * 2 + 1] = (rgb565 >> 8) & 0xFF;
                }
            }

            /* Trigger LVGL to redraw the image */
            lv_obj_invalidate(ui_Image_Thermal);
            ESP_LOGD(TAG, "Updated thermal image display (src: %ux%u -> dst: %ux%u)", LeptonEvent.Width, LeptonEvent.Height,
                     Image_Width, Image_Height);

            /* Update network frame for server streaming (if server is running) */
            if (Server_isRunning()) {
                if (xSemaphoreTake(_GUITask_State.NetworkFrame.mutex, 0) == pdTRUE) {
                    /* Convert scaled RGB565 buffer to RGB888 for network transmission */
                    uint8_t *rgb888_dst = _GUITask_State.NetworkRGBBuffer;
                    for (uint32_t i = 0; i < Image_Width * Image_Height; i++) {
                        /* Read RGB565 value (little endian) */
                        uint16_t rgb565 = dst[i * 2 + 0] | (dst[i * 2 + 1] << 8);

                        /* Convert RGB565 to RGB888 */
                        uint8_t r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
                        uint8_t g = ((rgb565 >> 5) & 0x3F) * 255 / 63;
                        uint8_t b = (rgb565 & 0x1F) * 255 / 31;

                        /* Store as RGB888 */
                        rgb888_dst[i * 3 + 0] = r;
                        rgb888_dst[i * 3 + 1] = g;
                        rgb888_dst[i * 3 + 2] = b;
                    }

                    _GUITask_State.NetworkFrame.buffer = _GUITask_State.NetworkRGBBuffer;
                    _GUITask_State.NetworkFrame.width = Image_Width;
                    _GUITask_State.NetworkFrame.height = Image_Height;
                    _GUITask_State.NetworkFrame.timestamp = esp_timer_get_time() / 1000;

                    /* Update temperatures from spotmeter data */
                    _GUITask_State.NetworkFrame.temp_min = _GUITask_State.SpotmeterInfo.Min;
                    _GUITask_State.NetworkFrame.temp_max = _GUITask_State.SpotmeterInfo.Max;
                    _GUITask_State.NetworkFrame.temp_avg = _GUITask_State.SpotmeterInfo.AverageTemperature;

                    xSemaphoreGive(_GUITask_State.NetworkFrame.mutex);
                }

                /* Notify Websocket handler that a new frame is ready (non-blocking) */
                if (WebSocket_Handler_HasClients()) {
                    WebSocket_Handler_NotifyFrameReady();
                }
            }
        }

        /* Process the recieved system events */
        EventBits = xEventGroupGetBits(_GUITask_State.EventGroup);
        if (EventBits & STOP_REQUEST) {

            xEventGroupClearBits(_GUITask_State.EventGroup, STOP_REQUEST);
            break;
        } else if (EventBits & BATTERY_VOLTAGE_READY) {
            char buf[8];

            if (_GUITask_State.BatteryInfo.Percentage == 100) {
                lv_obj_set_style_bg_color(ui_Image_Main_Battery, lv_color_hex(0x00FF00), 0);
                lv_label_set_text(ui_Image_Main_Battery, LV_SYMBOL_BATTERY_FULL);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 75) {
                lv_obj_set_style_bg_color(ui_Image_Main_Battery, lv_color_hex(0x00FF00), 0);
                lv_label_set_text(ui_Image_Main_Battery, LV_SYMBOL_BATTERY_3);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 50) {
                lv_obj_set_style_bg_color(ui_Image_Main_Battery, lv_color_hex(0xFFFF00), 0);
                lv_label_set_text(ui_Image_Main_Battery, LV_SYMBOL_BATTERY_2);
            } else if (_GUITask_State.BatteryInfo.Percentage >= 25) {
                lv_obj_set_style_bg_color(ui_Image_Main_Battery, lv_color_hex(0xFFFF00), 0);
                lv_label_set_text(ui_Image_Main_Battery, LV_SYMBOL_BATTERY_1);
            } else {
                lv_obj_set_style_bg_color(ui_Image_Main_Battery, lv_color_hex(0xFF0000), 0);
                lv_label_set_text(ui_Image_Main_Battery, LV_SYMBOL_BATTERY_EMPTY);
            }

            /* Update percentage text */
            snprintf(buf, sizeof(buf), "%d%%", _GUITask_State.BatteryInfo.Percentage);
            lv_label_set_text(ui_Label_Main_Battery_Remaining, buf);

            xEventGroupClearBits(_GUITask_State.EventGroup, BATTERY_VOLTAGE_READY);
        } else if ( EventBits & BATTERY_CHARGING_STATUS_READY) {
            xEventGroupClearBits(_GUITask_State.EventGroup, BATTERY_CHARGING_STATUS_READY);
        } else if (EventBits & WIFI_CONNECTION_STATE_CHANGED) {
            if (_GUITask_State.WiFiConnected) {
                char ip_buf[16];

                snprintf(ip_buf, sizeof(ip_buf), "%lu.%lu.%lu.%lu",
                         (_GUITask_State.IP_Info.IP >> 0) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 8) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 16) & 0xFF,
                         (_GUITask_State.IP_Info.IP >> 24) & 0xFF);

                lv_label_set_text(ui_Label_Info_IP, ip_buf);
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0x00FF00), LV_PART_MAIN);
                lv_obj_remove_flag(ui_Button_Main_WiFi, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_label_set_text(ui_Label_Info_IP, "Not connected");
                lv_obj_set_style_text_color(ui_Image_Main_WiFi, lv_color_hex(0xFF0000), LV_PART_MAIN);
                lv_obj_add_flag(ui_Button_Main_WiFi, LV_OBJ_FLAG_CLICKABLE);
            }

            xEventGroupClearBits(_GUITask_State.EventGroup, WIFI_CONNECTION_STATE_CHANGED);
        } else if (EventBits & PROVISIONING_STATE_CHANGED) {
            xEventGroupClearBits(_GUITask_State.EventGroup, PROVISIONING_STATE_CHANGED);
        } else if (EventBits & SD_CARD_STATE_CHANGED) {
            ESP_LOGI(TAG, "SD card state changed: %s", _GUITask_State.CardPresent ? "present" : "removed");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_STATE_CHANGED);
        } else if (EventBits & SD_CARD_MOUNTED) {
            ESP_LOGI(TAG, "SD card mounted - updating GUI");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_MOUNTED);
        } else if (EventBits & SD_CARD_MOUNT_ERROR) {
            ESP_LOGE(TAG, "SD card mount failed - keeping card present status");

            xEventGroupClearBits(_GUITask_State.EventGroup, SD_CARD_MOUNT_ERROR);
        } else if (EventBits & LEPTON_SPOTMETER_READY) {
            char temp_buf[16];

            /* Max temperature (top of gradient) */
            float temp_max_celsius = _GUITask_State.SpotmeterInfo.Max;
            snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", temp_max_celsius);
            lv_label_set_text(ui_Label_TempScaleMax, temp_buf);

            /* Min temperature (bottom of gradient) */
            float temp_min_celsius = _GUITask_State.SpotmeterInfo.Min;;
            snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", temp_min_celsius);
            lv_label_set_text(ui_Label_TempScaleMin, temp_buf);

            ESP_LOGD(TAG, "Updated spotmeter temperatures on GUI: Min=%.2f K, Max=%.2f K, Avg=%.2f K",
                     _GUITask_State.SpotmeterInfo.Min,
                     _GUITask_State.SpotmeterInfo.Max,
                     _GUITask_State.SpotmeterInfo.AverageTemperature);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_SPOTMETER_READY);
        } else if (EventBits & LEPTON_UPTIME_READY) {
            char buf[32];
            uint32_t uptime_sec;

            uptime_sec = _GUITask_State.LeptonUptime / 1000;

            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", uptime_sec / 3600, (uptime_sec % 3600) / 60, uptime_sec % 60);
            lv_label_set_text(ui_Label_Info_Lepton_Uptime, buf);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_UPTIME_READY);
        } else if (EventBits & LEPTON_TEMP_READY) {
            char buf[32];

            snprintf(buf, sizeof(buf), "%.2f °C", _GUITask_State.LeptonTemperatures.FPA);
            lv_label_set_text(ui_Label_Info_Lepton_FPA, buf);
            snprintf(buf, sizeof(buf), "%.2f °C", _GUITask_State.LeptonTemperatures.AUX);
            lv_label_set_text(ui_Label_Info_Lepton_AUX, buf);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_TEMP_READY);
        } else if (EventBits & LEPTON_PIXEL_TEMPERATURE_READY) {
            GUI_Helper_GetSpotTemperature(_GUITask_State.SpotTemperature);

            xEventGroupClearBits(_GUITask_State.EventGroup, LEPTON_PIXEL_TEMPERATURE_READY);
        }

        _lock_acquire(&_GUITask_State.LVGL_API_Lock);
        uint32_t time_till_next = lv_timer_handler();
        _lock_release(&_GUITask_State.LVGL_API_Lock);
        uint32_t delay_ms = (time_till_next > 0) ? time_till_next : 10;

        /* Reset watchdog at end of loop to prevent timeout during long operations */
        esp_task_wdt_reset();

        vTaskDelay(delay_ms / portTICK_PERIOD_MS);
    }

    esp_task_wdt_delete(NULL);
    vTaskDelete(NULL);
}

esp_err_t GUI_Task_Init(void)
{
    if (_GUITask_State.isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_ERROR_CHECK(GUI_Helper_Init(&_GUITask_State, XPT2046_LVGL_ReadCallback));

    /* Initialize the UI elements and trigger a redraw to make all elements accessibile */
    ui_init();

    /* Use the event loop to receive control signals from other tasks */
    esp_event_handler_register(DEVICE_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler, NULL);
    esp_event_handler_register(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler, NULL);
    esp_event_handler_register(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler, NULL);
    esp_event_handler_register(TIME_EVENTS, ESP_EVENT_ANY_ID, on_Time_Event_Handler, NULL);
    esp_event_handler_register(SD_EVENTS, ESP_EVENT_ANY_ID, on_SD_Event_Handler, NULL);

    _GUITask_State.ThermalCanvasBuffer = (uint8_t *)heap_caps_malloc(240 * 180 * 2, MALLOC_CAP_SPIRAM);
    _GUITask_State.GradientCanvasBuffer = (uint8_t *)heap_caps_malloc(20 * 180 * 2, MALLOC_CAP_SPIRAM);
    _GUITask_State.NetworkRGBBuffer = (uint8_t *)heap_caps_malloc(240 * 180 * 3, MALLOC_CAP_SPIRAM);

    if (_GUITask_State.ThermalCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate thermal canvas buffer!");
        return ESP_ERR_NO_MEM;
    }

    if (_GUITask_State.GradientCanvasBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate gradient canvas buffer!");
        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        return ESP_ERR_NO_MEM;
    }

    if (_GUITask_State.NetworkRGBBuffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate network RGB buffer!");
        heap_caps_free(_GUITask_State.ThermalCanvasBuffer);
        heap_caps_free(_GUITask_State.GradientCanvasBuffer);
        return ESP_ERR_NO_MEM;
    }

    /* Initialize buffers with black pixels (RGB565 = 0x0000) */
    memset(_GUITask_State.ThermalCanvasBuffer, 0x00, 240 * 180 * 2);
    memset(_GUITask_State.GradientCanvasBuffer, 0x00, 20 * 180 * 2);

    /* Now configure the image descriptors with allocated buffers */
    _GUITask_State.ThermalImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITask_State.ThermalImageDescriptor.header.w = 240;
    _GUITask_State.ThermalImageDescriptor.header.h = 180;
    _GUITask_State.ThermalImageDescriptor.data = _GUITask_State.ThermalCanvasBuffer;
    _GUITask_State.ThermalImageDescriptor.data_size = 240 * 180 * 2;

    _GUITask_State.GradientImageDescriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    _GUITask_State.GradientImageDescriptor.header.w = 20;
    _GUITask_State.GradientImageDescriptor.header.h = 180;
    _GUITask_State.GradientImageDescriptor.data = _GUITask_State.GradientCanvasBuffer;
    _GUITask_State.GradientImageDescriptor.data_size = 20 * 180 * 2;

    UI_Canvas_AddTempGradient();

    /* Set the images */
    lv_img_set_src(ui_Image_Thermal, &_GUITask_State.ThermalImageDescriptor);
    lv_img_set_src(ui_Image_Gradient, &_GUITask_State.GradientImageDescriptor);

#ifdef CONFIG_GUI_TOUCH_DEBUG
    /* Create touch debug visualization overlay on main screen */
    _GUITask_State.TouchDebugOverlay = lv_obj_create(ui_Main);
    lv_obj_set_size(_GUITask_State.TouchDebugOverlay, CONFIG_GUI_WIDTH, CONFIG_GUI_HEIGHT);
    lv_obj_set_pos(_GUITask_State.TouchDebugOverlay, 0, 0);
    lv_obj_set_style_bg_opa(_GUITask_State.TouchDebugOverlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_GUITask_State.TouchDebugOverlay, 0, 0);
    lv_obj_remove_flag(_GUITask_State.TouchDebugOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_GUITask_State.TouchDebugOverlay, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(_GUITask_State.TouchDebugOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(_GUITask_State.TouchDebugOverlay, -1); /* Move to top layer */

    /* Create touch indicator circle */
    _GUITask_State.TouchDebugCircle = lv_obj_create(_GUITask_State.TouchDebugOverlay);
    lv_obj_set_size(_GUITask_State.TouchDebugCircle, 20, 20);
    lv_obj_set_style_radius(_GUITask_State.TouchDebugCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_GUITask_State.TouchDebugCircle, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(_GUITask_State.TouchDebugCircle, LV_OPA_70, 0);
    lv_obj_set_style_border_color(_GUITask_State.TouchDebugCircle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(_GUITask_State.TouchDebugCircle, 2, 0);
    lv_obj_add_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_GUITask_State.TouchDebugCircle, LV_OBJ_FLAG_SCROLLABLE);

    /* Create coordinate label */
    _GUITask_State.TouchDebugLabel = lv_label_create(_GUITask_State.TouchDebugOverlay);
    lv_obj_set_pos(_GUITask_State.TouchDebugLabel, 5, 5);
    lv_label_set_text(_GUITask_State.TouchDebugLabel, "Touch Debug");
    lv_obj_set_style_text_color(_GUITask_State.TouchDebugLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(_GUITask_State.TouchDebugLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_GUITask_State.TouchDebugLabel, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(_GUITask_State.TouchDebugLabel, 3, 0);
    lv_obj_add_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_GUITask_State.TouchDebugLabel, LV_OBJ_FLAG_CLICKABLE);

    ESP_LOGI(TAG, "Touch debug visualization enabled on ui_Main screen");
#endif

    /* Register event handler for ROI touch interactions (drag & resize) */
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(ui_Image_Main_Thermal_ROI, ROI_Rectangle_Event_Handler, LV_EVENT_RELEASED, NULL);

    /* Register ROI button events: long-press for reset, click for toggle */
    lv_obj_add_event_cb(ui_Button_Main_ROI, ROI_Clicked_Event_Handler, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(ui_Button_Main_ROI, ROI_Clicked_Event_Handler, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "ROI event handlers registered and configured for touch interaction");

    _GUITask_State.ROI_EditMode = false;
    _GUITask_State.ROI_LongPressActive = false;

    /* Initialize network frame for server streaming */
    _GUITask_State.NetworkFrame.mutex = xSemaphoreCreateMutex();
    if (_GUITask_State.NetworkFrame.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create NetworkFrame mutex!");
        return ESP_ERR_NO_MEM;
    }

    _GUITask_State.isInitialized = true;

    return ESP_OK;
}

void GUI_Task_Deinit(void)
{
    if (_GUITask_State.isInitialized == false) {
        return;
    }

    esp_event_handler_unregister(DEVICE_EVENTS, ESP_EVENT_ANY_ID, on_Devices_Event_Handler);
    esp_event_handler_unregister(NETWORK_EVENTS, ESP_EVENT_ANY_ID, on_Network_Event_Handler);
    esp_event_handler_unregister(LEPTON_EVENTS, ESP_EVENT_ANY_ID, on_Lepton_Event_Handler);
    esp_event_handler_unregister(TIME_EVENTS, ESP_EVENT_ANY_ID, on_Time_Event_Handler);
    esp_event_handler_unregister(SD_EVENTS, ESP_EVENT_ANY_ID, on_SD_Event_Handler);

    ui_destroy();

    GUI_Helper_Deinit(&_GUITask_State);

    if (_GUITask_State.NetworkFrame.mutex != NULL) {
        vSemaphoreDelete(_GUITask_State.NetworkFrame.mutex);
        _GUITask_State.NetworkFrame.mutex = NULL;
    }

    _GUITask_State.Display = NULL;
    _GUITask_State.isInitialized = false;
}

esp_err_t GUI_Task_Start(App_Context_t *p_AppContext)
{
    BaseType_t ret;

    if (p_AppContext == NULL) {
        return ESP_ERR_INVALID_ARG;
    } else if (_GUITask_State.isInitialized == false) {
        return ESP_ERR_INVALID_STATE;
    } else if (_GUITask_State.Running) {
        ESP_LOGW(TAG, "Task already running");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Starting GUI Task");

    ret = xTaskCreatePinnedToCore(
              Task_GUI,
              "Task_GUI",
              CONFIG_GUI_TASK_STACKSIZE,
              p_AppContext,
              CONFIG_GUI_TASK_PRIO,
              &_GUITask_State.GUI_Handle,
              CONFIG_GUI_TASK_CORE
          );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create GUI task: %d!", ret);
        return ESP_ERR_NO_MEM;
    }

    _GUITask_State.Running = true;

    return ESP_OK;
}

esp_err_t GUI_Task_Stop(void)
{
    if (_GUITask_State.Running == false) {
        return ESP_OK;
    }

    vTaskDelete(_GUITask_State.GUI_Handle);

    _GUITask_State.GUI_Handle = NULL;
    _GUITask_State.Running = false;

    return ESP_OK;
}

bool GUI_Task_isRunning(void)
{
    return _GUITask_State.Running;
}