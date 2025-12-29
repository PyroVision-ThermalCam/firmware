/*
 * guiHelper.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Helper functions for the GUI task.
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

#include "guiHelper.h"
#include "Application/application.h"
#include "../Export/ui.h"

#if defined(CONFIG_LCD_SPI2_HOST)
#define LCD_SPI_HOST                        SPI2_HOST
#elif defined(CONFIG_LCD_SPI3_HOST)
#define LCD_SPI_HOST                        SPI3_HOST
#else
#error "No SPI host defined for LCD!"
#endif

#define GUI_DRAW_BUFFER_SIZE                (CONFIG_GUI_WIDTH * CONFIG_GUI_HEIGHT * sizeof(uint16_t) / 10)

static const esp_lcd_panel_dev_config_t _GUI_Panel_Config = {
    .reset_gpio_num = CONFIG_LCD_RST,
    .color_space = ESP_LCD_COLOR_SPACE_BGR,
    .bits_per_pixel = 16,
    .flags = {
        .reset_active_high = 0,
    },
    .vendor_config = NULL,
};

static esp_lcd_panel_io_spi_config_t _GUI_Panel_IO_Config = {
    .cs_gpio_num = CONFIG_LCD_CS,
    .dc_gpio_num = CONFIG_LCD_DC,
    .spi_mode = 0,
    .pclk_hz = CONFIG_LCD_CLOCK,
    .trans_queue_depth = 10,
    .on_color_trans_done = NULL,
    .user_ctx = NULL,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .flags = {
        .dc_low_on_data = 0,
        .octal_mode = 0,
        .sio_mode = 0,
        .lsb_first = 0,
        .cs_high_active = 0,
    },
};

static esp_lcd_touch_config_t _GUI_Touch_Config = {
    .x_max = CONFIG_GUI_WIDTH,
    .y_max = CONFIG_GUI_HEIGHT,
    .rst_gpio_num = static_cast<gpio_num_t>(CONFIG_TOUCH_RST),
    .int_gpio_num = static_cast<gpio_num_t>(CONFIG_TOUCH_IRQ),
    .flags = {
        .swap_xy = 0,
        .mirror_x = 0,
        .mirror_y = 0,
    },
};

static esp_lcd_panel_io_spi_config_t _GUI_Touch_IO_Config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(CONFIG_TOUCH_CS);

static const char *TAG = "gui_helper";

static void GUI_LVGL_TickTimer_CB(void *p_Arg)
{
    lv_tick_inc(CONFIG_GUI_LVGL_TICK_PERIOD_MS);
}

static void GUI_LCD_Flush_CB(lv_display_t *p_Disp, const lv_area_t *p_Area, uint8_t *p_PxMap)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(p_Disp);

    int offsetx1 = p_Area->x1;
    int offsetx2 = p_Area->x2;
    int offsety1 = p_Area->y1;
    int offsety2 = p_Area->y2;

    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, p_PxMap);

    lv_display_flush_ready(p_Disp);
}

esp_err_t GUI_Helper_Init(GUI_Task_State_t *p_GUITask_State, lv_indev_read_cb_t Touch_Read_Callback)
{
#if CONFIG_LCD_BL >= 0
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << CONFIG_LCD_BL,
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(CONFIG_LCD_BL), LCD_BK_LIGHT_ON_LEVEL);
#endif

    _GUI_Panel_IO_Config.on_color_trans_done = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_SPI_HOST), &_GUI_Panel_IO_Config,
                                             &p_GUITask_State->Panel_IO_Handle));
    ESP_LOGD(TAG, "SPI panel IO created");

    ESP_LOGD(TAG, "Initialize LVGL library");
    lv_init();

    /* Initialize LVGL display first (needed for callback) */
    p_GUITask_State->Display = lv_display_create(CONFIG_GUI_WIDTH, CONFIG_GUI_HEIGHT);
    ESP_LOGD(TAG, "LVGL display object created");

    /* Register event callbacks BEFORE installing panel driver */
    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(p_GUITask_State->Panel_IO_Handle, &cbs,
                                                              p_GUITask_State->Display));
    ESP_LOGD(TAG, "LCD panel IO callbacks registered");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(p_GUITask_State->Panel_IO_Handle, &_GUI_Panel_Config,
                                              &p_GUITask_State->PanelHandle));
    ESP_LOGD(TAG, "ILI9341 panel driver installed");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(p_GUITask_State->PanelHandle));
    ESP_LOGD(TAG, "Panel reset complete");
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ESP_ERROR_CHECK(esp_lcd_panel_init(p_GUITask_State->PanelHandle));
    ESP_LOGD(TAG, "Panel initialized");
    vTaskDelay(100 / portTICK_PERIOD_MS);

    /* Set to landscape mode (320x240) - swap width and height */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(p_GUITask_State->PanelHandle, true));
    ESP_LOGD(TAG, "Panel swap_xy enabled for landscape");

    /* 180 degree rotation: mirror both X and Y */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(p_GUITask_State->PanelHandle, true, true));
    ESP_LOGD(TAG, "Panel mirroring configured (180 degree rotation)");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(p_GUITask_State->PanelHandle, true));
    ESP_LOGD(TAG, "Panel display turned ON");
    vTaskDelay(100 / portTICK_PERIOD_MS);

    /* Set LVGL display properties */
    lv_display_set_flush_cb(p_GUITask_State->Display, GUI_LCD_Flush_CB);
    lv_display_set_user_data(p_GUITask_State->Display,
                             p_GUITask_State->PanelHandle);

    /* Note: Color format set to RGB565 by default, matching thermal image BGR565 conversion */
    /* Allocate draw buffers in PSRAM */
    p_GUITask_State->DisplayBuffer1 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    p_GUITask_State->DisplayBuffer2 = heap_caps_malloc(GUI_DRAW_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    ESP_LOGD(TAG, "Allocated LVGL buffers: %d bytes each in PSRAM", GUI_DRAW_BUFFER_SIZE);
    if ((p_GUITask_State->DisplayBuffer1 == NULL) || (p_GUITask_State->DisplayBuffer2 == NULL)) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers!");
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(p_GUITask_State->Display,
                           p_GUITask_State->DisplayBuffer1,
                           p_GUITask_State->DisplayBuffer2,
                           GUI_DRAW_BUFFER_SIZE,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGD(TAG, "LVGL display buffers configured");

    p_GUITask_State->EventGroup = xEventGroupCreate();
    if (p_GUITask_State->EventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create GUI event group!");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "Initialize touch controller XPT2046");
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(LCD_SPI_HOST), &_GUI_Touch_IO_Config,
                                             &p_GUITask_State->Touch_IO_Handle));
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(p_GUITask_State->Touch_IO_Handle, &_GUI_Touch_Config,
                                                  &p_GUITask_State->TouchHandle));

    /* Register touchpad input device */
    ESP_LOGD(TAG, "Register touch input device to LVGL");
    p_GUITask_State->Touch = lv_indev_create();
    lv_indev_set_type(p_GUITask_State->Touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(p_GUITask_State->Touch, p_GUITask_State->Display);
    lv_indev_set_read_cb(p_GUITask_State->Touch, Touch_Read_Callback);
    lv_indev_set_user_data(p_GUITask_State->Touch, p_GUITask_State->TouchHandle);

    /* Create LVGL tick timer */
    const esp_timer_create_args_t LVGL_TickTimer_args = {
        .callback = &GUI_LVGL_TickTimer_CB,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false,
    };

    ESP_ERROR_CHECK(esp_timer_create(&LVGL_TickTimer_args, &p_GUITask_State->LVGL_TickTimer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(p_GUITask_State->LVGL_TickTimer, CONFIG_GUI_LVGL_TICK_PERIOD_MS * 1000));

    /* Create LVGL clock update timer. Use a 100 ms interval for smoother updates */
    p_GUITask_State->UpdateTimer[0] = lv_timer_create(GUI_Helper_Timer_ClockUpdate, 100, NULL);

    p_GUITask_State->UpdateTimer[1] = lv_timer_create(GUI_Helper_Timer_SpotUpdate, 2000, NULL);
    p_GUITask_State->UpdateTimer[2] = lv_timer_create(GUI_Helper_Timer_SpotmeterUpdate, 5000, NULL);

    return ESP_OK;
}

void GUI_Helper_Deinit(GUI_Task_State_t *p_GUITask_State)
{
    if (p_GUITask_State->isInitialized == false) {
        return;
    }

    for (uint32_t i = 0; i < sizeof(p_GUITask_State->UpdateTimer) / sizeof(p_GUITask_State->UpdateTimer[0]); i++) {
        if (p_GUITask_State->UpdateTimer[i] != NULL) {
            lv_timer_delete(p_GUITask_State->UpdateTimer[i]);
            p_GUITask_State->UpdateTimer[i] = NULL;
        }
    }

    esp_timer_stop(p_GUITask_State->LVGL_TickTimer);
    esp_timer_delete(p_GUITask_State->LVGL_TickTimer);
    p_GUITask_State->LVGL_TickTimer = NULL;

    if (p_GUITask_State->LVGL_TickTimer != NULL) {
        esp_timer_stop(p_GUITask_State->LVGL_TickTimer);
        esp_timer_delete(p_GUITask_State->LVGL_TickTimer);
        p_GUITask_State->LVGL_TickTimer = NULL;
    }

    if (p_GUITask_State->EventGroup != NULL) {
        vEventGroupDelete(p_GUITask_State->EventGroup);
        p_GUITask_State->EventGroup = NULL;
    }

    if (p_GUITask_State->Touch != NULL) {
        lv_indev_delete(p_GUITask_State->Touch);
        p_GUITask_State->Touch = NULL;
    }

    esp_lcd_touch_del(p_GUITask_State->TouchHandle);

    if (p_GUITask_State->Display != NULL) {
        lv_display_delete(p_GUITask_State->Display);
        p_GUITask_State->Display = NULL;
    }

    if (p_GUITask_State->DisplayBuffer1 != NULL) {
        heap_caps_free(p_GUITask_State->DisplayBuffer1);
        p_GUITask_State->DisplayBuffer1 = NULL;
    }

    if (p_GUITask_State->DisplayBuffer2 != NULL) {
        heap_caps_free(p_GUITask_State->DisplayBuffer2);
        p_GUITask_State->DisplayBuffer2 = NULL;
    }

    if (p_GUITask_State->Touch_IO_Handle != NULL) {
        esp_lcd_panel_io_del(p_GUITask_State->Touch_IO_Handle);
        p_GUITask_State->Touch_IO_Handle = NULL;
    }

    if (p_GUITask_State->PanelHandle != NULL) {
        esp_lcd_panel_del(p_GUITask_State->PanelHandle);
        p_GUITask_State->PanelHandle = NULL;
    }
}

void GUI_Helper_GetSpotTemperature(float Temperature)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%.2f °C", Temperature);
    lv_label_set_text(ui_Label_Main_Thermal_PixelTemperature, buf);
}

void GUI_Helper_Timer_ClockUpdate(lv_timer_t *p_Timer)
{
    char buf[9];
    struct tm time_now;

    /* Get current time from TimeManager */
    TimeManager_GetTime(&time_now, NULL);

    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", time_now.tm_hour, time_now.tm_min, time_now.tm_sec);
    lv_label_set_text(ui_Label_Main_Time, buf);

    /* Broadcast telemetry to WebSocket clients if server is running */
    if (Server_isRunning()) {
        WebSocket_Handler_BroadcastTelemetry();
    }
}

void GUI_Helper_Timer_SpotUpdate(lv_timer_t *p_Timer)
{
    App_GUI_Screenposition_t ScreenPosition;

    /* Check if thermal image is initialized */
    if ((lv_obj_get_width(ui_Image_Thermal) == 0) || (lv_obj_get_height(ui_Image_Thermal) == 0)) {
        ESP_LOGW(TAG, "Thermal image not yet initialized, skipping spot update",
                 lv_obj_get_width(ui_Image_Thermal), lv_obj_get_height(ui_Image_Thermal));
        return;
    }

    /* Get crosshair position relative to its parent (ui_Image_Thermal) */
    ScreenPosition.x = lv_obj_get_x(ui_Label_Main_Thermal_Crosshair) + lv_obj_get_width(
                           ui_Label_Main_Thermal_Crosshair) / 2;
    ScreenPosition.y = lv_obj_get_y(ui_Label_Main_Thermal_Crosshair) + lv_obj_get_height(
                           ui_Label_Main_Thermal_Crosshair) / 2;
    ScreenPosition.Width = lv_obj_get_width(ui_Image_Thermal);
    ScreenPosition.Height = lv_obj_get_height(ui_Image_Thermal);

    ESP_LOGD(TAG, "Crosshair center in thermal canvas: (%d,%d), size (%d,%d)", ScreenPosition.x, ScreenPosition.y, ScreenPosition.Width, ScreenPosition.Height);

    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_PIXEL_TEMPERATURE, &ScreenPosition, sizeof(ScreenPosition), portMAX_DELAY);
}

void GUI_Helper_Timer_SpotmeterUpdate(lv_timer_t *p_Timer)
{
    esp_event_post(GUI_EVENTS, GUI_EVENT_REQUEST_SPOTMETER, NULL, 0, portMAX_DELAY);
}