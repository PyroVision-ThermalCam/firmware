/*
 * networkTask.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Network task for WiFi and HTTP server management.
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

#ifndef NETWORK_TASK_H_
#define NETWORK_TASK_H_

#include <esp_err.h>
#include <esp_event.h>

#include <stdint.h>

#include "Application/application.h"
#include "Application/Manager/Network/network_types.h"

esp_err_t Network_Task_Init(App_Context_t *p_AppContext);

void Network_Task_Deinit(void);

esp_err_t Network_Task_Start(App_Context_t *p_AppContext);

esp_err_t Network_Task_Stop(void);

bool Network_Task_isRunning(void);

#endif /* NETWORK_TASK_H_ */