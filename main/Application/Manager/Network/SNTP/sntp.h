/*
 * sntp.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: SNTP management.
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

#ifndef SNTP_H_
#define SNTP_H_

#include <esp_err.h>

#include <stdint.h>

/** @brief  Initialize SNTP and event handlers.
 *  @return ESP_OK on success
 */
esp_err_t SNTP_Init(void);

/** @brief  Deinitialize SNTP and event handlers.
 *  @return ESP_OK on success
 */
esp_err_t SNTP_Deinit(void);

/** @brief          Initialize SNTP and synchronize time.
 *  @param Retries  Number of retries to get time
 *  @return         ESP_OK on success
 */
esp_err_t SNTP_GetTime(uint8_t Retries);

#endif /* SNTP_H_ */