/*
 * dnsServer.h
 *
 *  Copyright (C) Daniel Kampert, 2026
 *  Website: www.kampis-elektroecke.de
 *  File info: Simple DNS server for captive portal.
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

#ifndef DNSSERVER_H_
#define DNSSERVER_H_

#include <esp_err.h>

/** @brief  Start DNS server for captive portal.
 *          Redirects all DNS queries to the ESP32's IP address.
 *  @return ESP_OK on success
 */
esp_err_t DNS_Server_Start(void);

/** @brief  Stop DNS server.
 */
void DNS_Server_Stop(void);

#endif /* DNSSERVER_H_ */
