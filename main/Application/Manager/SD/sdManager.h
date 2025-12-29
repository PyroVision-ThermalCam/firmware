/*
 * sdManager.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: SD card manager header.
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

#ifndef SDMANAGER_H_
#define SDMANAGER_H_

#include <esp_err.h>

#include <stdint.h>
#include <stdbool.h>

/** @brief SD card mount point.
 */
#define SD_MOUNT_POINT      "/sdcard"

/** @brief SD card event identifiers.
 */
enum {
    SD_EVENT_CARD_CHANGED,                      /**< SD card has been inserted or removed.
                                                     The data type is bool. */
    SD_EVENT_MOUNTED,                           /**< SD card has been mounted successfully. */
    SD_EVENT_MOUNT_ERROR,                       /**< SD card mount error occurred. */
};

/** @brief  Initialize the SD card manager.
 *          Sets up card detection GPIO and attempts initial mount if card is present.
 *  @return ESP_OK on success
 */
esp_err_t SDManager_Init(void);

/** @brief  Deinitialize the SD card manager.
 *          Unmounts card if present and releases resources.
 *  @return ESP_OK on success
 */
esp_err_t SDManager_Deinit(void);

/** @brief  Check if SD card is currently present and mounted.
 *  @return true if card is present and mounted, false otherwise
 */
bool SDManager_IsCardPresent(void);

/** @brief          Save a bitmap image to the SD card.
 *  @param p_Path   Path to save the bitmap file (relative to mount point)
 *  @param Width    Image width in pixels
 *  @param Height   Image height in pixels
 *  @param p_Buffer Pointer to RGB888 image buffer
 *  @param Length   Buffer length in bytes
 *  @return         ESP_OK on success
 */
esp_err_t SDManager_SaveBitmap(const char *p_Path, uint16_t Width, uint16_t Height, const uint8_t *p_Buffer,
                               uint32_t Length);

/** @brief  Mount the SD card.
 *  @return ESP_OK on success
 */
esp_err_t SDManager_Mount(void);

/** @brief  Unmount the SD card.
 *  @return ESP_OK on success
 */
esp_err_t SDManager_Unmount(void);

#endif /* SDMANAGER_H_ */
