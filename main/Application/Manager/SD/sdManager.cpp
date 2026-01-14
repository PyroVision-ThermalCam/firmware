/*
 * sdManager.cpp
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: SD card manager implementation.
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
#include <esp_check.h>
#include <esp_event.h>
#include <esp_vfs_fat.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#include <sdmmc_cmd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <cstring>

#include "sdManager.h"
#include "Application/application.h"
#include "Application/Manager/Devices/SPI/spi.h"

#if defined(CONFIG_SD_CARD_SPI2_HOST)
#define SD_SPI_HOST                     SPI2_HOST
#elif defined(CONFIG_SD_CARD_SPI3_HOST)
#define SD_SPI_HOST                     SPI3_HOST
#else
#error "No SPI host defined for SD card!"
#endif

#if CONFIG_SD_CARD_PIN_CD > 0
#define SD_DEBOUNCE_TIME_MS     500
#endif

#define SD_MOUNT_TASK_STACK     4096
#define SD_MOUNT_TASK_PRIORITY  3

ESP_EVENT_DEFINE_BASE(SD_EVENTS);

static const char *TAG = "sd-manager";

typedef struct {
    sdmmc_card_t *Card;
    sdmmc_host_t Host;
    sdspi_device_config_t SlotConfig;
    bool isInitialized;
    esp_timer_handle_t DebounceTimer;
    esp_vfs_fat_sdmmc_mount_config_t MountConfig;
    TaskHandle_t MountTaskHandle;
    volatile bool CardPresent;
    gpio_config_t CD_Conf;
} SD_Manager_State_t;

static SD_Manager_State_t _SD_Manager_State;

#if CONFIG_SD_CARD_PIN_CD > 0
/** @brief          Debounce timer callback - called after card state is stable.
 *  @param p_Arg    Unused
 */
static void SDManager_DebounceTimerCallback(void *p_Arg)
{
    (void)p_Arg;

    /* Read current card state */
    bool cardPresent = (gpio_get_level(static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CD)) == 0);

    ESP_LOGI(TAG, "Card state stable: %s", cardPresent ? "present" : "removed");

    _SD_Manager_State.CardPresent = cardPresent;

    /* Notify about state change */
    esp_event_post(SD_EVENTS, SD_EVENT_CARD_CHANGED, (const void *)&cardPresent, sizeof(bool), 0);
}

/** @brief          GPIO interrupt handler for card detection.
 *                  Called when the card detect pin changes state.
 *  @param p_Arg    Unused
 */
static void IRAM_ATTR SDManager_CardDetectISR(void *p_Arg)
{
    (void)p_Arg;

    /* Restart debounce timer on every edge - only triggers callback after stable period */
    if (_SD_Manager_State.DebounceTimer != NULL) {
        // TODO: Not IRAM save
        esp_timer_stop(_SD_Manager_State.DebounceTimer);
        esp_timer_start_once(_SD_Manager_State.DebounceTimer, SD_DEBOUNCE_TIME_MS * 1000);
    }
}
#endif

esp_err_t SDManager_Init(void)
{
    if (_SD_Manager_State.isInitialized) {
        ESP_LOGW(TAG, "SD Manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing SD Manager...");

    if (CONFIG_SD_CARD_PIN_CS < 0) {
        ESP_LOGE(TAG, "Invalid CS pin!");
        return ESP_ERR_INVALID_ARG;
    }

    _SD_Manager_State.MountConfig = {
#ifdef CONFIG_SD_CARD_FORMAT_CARD
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false
    };

    _SD_Manager_State.Host = SDSPI_HOST_DEFAULT();
    _SD_Manager_State.Host.slot = SD_SPI_HOST;
    _SD_Manager_State.SlotConfig = SDSPI_DEVICE_CONFIG_DEFAULT();
    _SD_Manager_State.SlotConfig.gpio_cs = static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CS);
    _SD_Manager_State.SlotConfig.host_id = static_cast<spi_host_device_t>(_SD_Manager_State.Host.slot);

    if (esp_vfs_fat_sdspi_mount("/sdcard", &_SD_Manager_State.Host, &_SD_Manager_State.SlotConfig,
                                &_SD_Manager_State.MountConfig, &_SD_Manager_State.Card) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card filesystem!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD card filesystem mounted successfully");

#if CONFIG_SD_CARD_PIN_CD > 0
    esp_err_t Error;

    /* Configure card detect pin with interrupt */
    _SD_Manager_State.CD_Conf.intr_type = GPIO_INTR_ANYEDGE;
    _SD_Manager_State.CD_Conf.mode = GPIO_MODE_INPUT;
    _SD_Manager_State.CD_Conf.pin_bit_mask = (1ULL << CONFIG_SD_CARD_PIN_CD);
    _SD_Manager_State.CD_Conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    _SD_Manager_State.CD_Conf.pull_up_en = GPIO_PULLUP_ENABLE;

    Error = gpio_config(&_SD_Manager_State.CD_Conf);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure card detect pin: %d!", Error);
        return Error;
    }

    /* Install GPIO ISR service if not already installed */
    Error = gpio_install_isr_service(0);
    if (Error != ESP_OK && Error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %d!", Error);
        return Error;
    }

    /* Add ISR handler for card detect pin */
    Error = gpio_isr_handler_add(static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CD), SDManager_CardDetectISR, NULL);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %d!", Error);
        return Error;
    }

    /* Create debounce timer */
    const esp_timer_create_args_t debounce_timer_args = {
        .callback = &SDManager_DebounceTimerCallback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "sd_debounce",
        .skip_unhandled_events = false,
    };

    Error = esp_timer_create(&debounce_timer_args, &_SD_Manager_State.DebounceTimer);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create debounce timer: %d!", Error);
        return Error;
    }

    /* Check initial card state and trigger debounced event if present */
    if (gpio_get_level(static_cast<gpio_num_t>(CONFIG_SD_CARD_PIN_CD)) == 0) {
        ESP_LOGI(TAG, "SD card present at initialization");

        _SD_Manager_State.CardPresent = true;

        /* Start timer to trigger initial mount after debounce period */
        esp_timer_start_once(_SD_Manager_State.DebounceTimer, SD_DEBOUNCE_TIME_MS * 1000);
    } else {
        ESP_LOGI(TAG, "No SD card present at initialization");

        _SD_Manager_State.CardPresent = false;
    }
#endif

    _SD_Manager_State.isInitialized = true;

    ESP_LOGI(TAG, "SD manager initialized");

    return ESP_OK;
}

esp_err_t SDManager_Deinit(void)
{
    if (_SD_Manager_State.isInitialized == false) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing SD Manager...");

#if CONFIG_SD_CARD_PIN_CD > 0
    /* Stop and delete debounce timer */
    if (_SD_Manager_State.DebounceTimer != NULL) {
        esp_timer_stop(_SD_Manager_State.DebounceTimer);
        esp_timer_delete(_SD_Manager_State.DebounceTimer);
        _SD_Manager_State.DebounceTimer = NULL;
    }
#endif /* CONFIG_SD_CARD_PIN_CD */

    /* Wait for mount task to finish if running */
    if (_SD_Manager_State.MountTaskHandle != NULL) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        _SD_Manager_State.MountTaskHandle = NULL;
    }

    SDManager_Unmount();

    _SD_Manager_State.isInitialized = false;

    ESP_LOGI(TAG, "SD Manager deinitialized");

    return ESP_OK;
}

bool SDManager_isCardPresent(void)
{
    return _SD_Manager_State.CardPresent;
}

/** @brief      Background task for mounting SD card (prevents GUI blocking).
 *  @param arg  Unused
 */
static void SDManager_MountTask(void *arg)
{
    (void)arg;
    esp_err_t Error;

    /* Add this task to WDT */
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "Mount task started");

    /* Reset WDT before mount attempt */
    esp_task_wdt_reset();

    Error = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &_SD_Manager_State.Host, &_SD_Manager_State.SlotConfig,
                                    &_SD_Manager_State.MountConfig,
                                    &_SD_Manager_State.Card);

    /* Reset WDT after mount attempt */
    esp_task_wdt_reset();
    if (Error != ESP_OK) {
        if (Error == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Format the card or check hardware connection!");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card %d!", Error);
        }

        _SD_Manager_State.Card = NULL;

        /* Post mount error event */
        esp_event_post(SD_EVENTS, SD_EVENT_MOUNT_ERROR, NULL, 0, portMAX_DELAY);
    } else {
        /* Card mounted successfully */
        ESP_LOGI(TAG, "SD card mounted successfully");
        ESP_LOGI(TAG, "Card size: %llu MB", ((uint64_t)_SD_Manager_State.Card->csd.capacity) *
                 _SD_Manager_State.Card->csd.sector_size /
                 (1024 * 1024));

        /* Post mount success event */
        esp_event_post(SD_EVENTS, SD_EVENT_MOUNTED, NULL, 0, portMAX_DELAY);
    }

    _SD_Manager_State.MountTaskHandle = NULL;

    /* Remove from WDT before deleting */
    esp_task_wdt_delete(NULL);

    vTaskDelete(NULL);
}

esp_err_t SDManager_Mount(void)
{
    if (_SD_Manager_State.CardPresent == false) {
        ESP_LOGW(TAG, "No SD card available!");
        return ESP_ERR_NOT_FOUND;
    }

    /* Prevent multiple mount tasks */
    if (_SD_Manager_State.MountTaskHandle != NULL) {
        ESP_LOGW(TAG, "Mount task already running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting async mount task...");

    BaseType_t ret = xTaskCreatePinnedToCore(
                         SDManager_MountTask,
                         "sd_mount",
                         SD_MOUNT_TASK_STACK,
                         NULL,
                         SD_MOUNT_TASK_PRIORITY,
                         &_SD_Manager_State.MountTaskHandle,
                         1  /* Run on CPU 1 to avoid blocking GUI task on CPU 0 */
                     );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create mount task!");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t SDManager_Unmount(void)
{
    esp_err_t Error;

    if (_SD_Manager_State.Card == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Unmounting SD card...");

    Error = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, _SD_Manager_State.Card);
    if (Error != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount SD card %d!", Error);

        return Error;
    }

    ESP_LOGI(TAG, "SD card unmounted");

    _SD_Manager_State.Card = NULL;

    return ESP_OK;
}