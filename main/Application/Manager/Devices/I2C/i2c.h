/*
 * i2c.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: I2C master driver interface.
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

#ifndef I2C_H_
#define I2C_H_

#include <stdint.h>
#include <stdbool.h>

#include <driver/i2c_master.h>

#include <esp_err.h>

/** @brief              Initialize the I2C driver.
 *  @param p_Config     Pointer to configuration options
 *  @param p_Bus_Handle Pointer to store the created bus handle
 *  @return             ESP_OK when successful
 */
int32_t I2CM_Init(i2c_master_bus_config_t *p_Config, i2c_master_bus_handle_t *p_Bus_Handle);

/** @brief              Deinitialize the I2C driver.
 *  @param Bus_Handle   I2C bus handle
 *  @return             ESP_OK when successful
 */
int32_t I2CM_Deinit(i2c_master_bus_handle_t Bus_Handle);

/** @brief              Transmit data over the I2C interface.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param p_Data       Pointer to data
 *  @param Length       Length of data in bytes
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
int32_t I2CM_Write(i2c_master_dev_handle_t *p_Dev_Handle, const uint8_t *p_Data, uint32_t Length);

/** @brief              Receive data from the I2C interface.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param p_Data       Pointer to data
 *  @param Length       Length of data in bytes
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
int32_t I2CM_Read(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t *p_Data, uint32_t Length);

/** @brief              Modify the content of a register.
 *  @param p_Dev_Handle Pointer to I2C device handle
 *  @param Register     Register address
 *  @param Mask         Bit mask
 *  @param Value        Bit level
 *  @return             ESP_OK when successful
 *                      ESP_ERR_INVALID_ARG when an invalid argument is passed into the function
 *                      ESP_ERR_INVALID_STATE when the I2C interface isn´t initialized
 */
int32_t I2CM_ModifyRegister(i2c_master_dev_handle_t *p_Dev_Handle, uint8_t Register, uint8_t Mask, uint8_t Value);

#endif /* I2C_H_ */