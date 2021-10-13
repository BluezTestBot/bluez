/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2011-2014  Intel Corporation
 *  Copyright (C) 2002-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <stdint.h>

#define MSFT_SUBCMD_READ_SUPPORTED_FEATURES	0x00

struct msft_cmd_read_supported_features {
	uint8_t subcmd;
};

struct msft_rsp_read_supported_features {
	uint8_t  status;
	uint8_t  subcmd;
	uint8_t  features[8];
	uint8_t  evt_prefix_len;
	uint8_t  evt_prefix[];
};

#define MSFT_SUBCMD_MONITOR_RSSI		0x01

struct msft_cmd_monitor_rssi {
	uint8_t  subcmd;
	uint16_t handle;
	int8_t   rssi_high;
	int8_t   rssi_low;
	uint8_t  rssi_low_interval;
	uint8_t  rssi_period;
};

struct msft_rsp_monitor_rssi {
	uint8_t  status;
	uint8_t  subcmd;
};

#define MSFT_SUBCMD_CANCEL_MONITOR_RSSI		0x02

struct msft_cmd_cancel_monitor_rssi {
	uint8_t  subcmd;
	uint16_t handle;
};

struct msft_rsp_cancel_monitor_rssi {
	uint8_t  status;
	uint8_t  subcmd;
};

#define MSFT_SUBCMD_LE_MONITOR_ADV		0x03

struct msft_le_monitor_pattern {
	uint8_t  len;
	uint8_t  type;
	uint8_t  start;
	uint8_t  data[];
};

struct msft_le_monitor_adv_pattern_type {
	uint8_t num_patterns;
	struct msft_le_monitor_pattern data[];
};

struct msft_le_monitor_adv_uuid_type {
	uint8_t  type;
	union {
		uint16_t u16;
		uint32_t u32;
		uint8_t  u128[8];
	} value;
};

struct msft_le_monitor_adv_irk_type {
	uint8_t  irk[8];
	uint8_t  addr_type;
	uint8_t  addr[6];
};

struct msft_cmd_le_monitor_adv {
	uint8_t  subcmd;
	int8_t   rssi_low;
	int8_t   rssi_high;
	uint8_t  rssi_low_interval;
	uint8_t  rssi_period;
	uint8_t  type;
	uint8_t  data[];
};

struct msft_rsp_le_monitor_adv {
	uint8_t  status;
	uint8_t  subcmd;
	uint8_t  handle;
};

#define MSFT_SUBCMD_LE_CANCEL_MONITOR_ADV	0x04

struct msft_cmd_le_cancel_monitor_adv {
	uint8_t  subcmd;
	uint8_t  handle;
};

struct msft_rsp_le_cancel_monitor_adv {
	uint8_t  status;
	uint8_t  subcmd;
};

#define MSFT_SUBCMD_LE_MONITOR_ADV_ENABLE	0x05

struct msft_cmd_le_monitor_adv_enable {
	uint8_t  subcmd;
	uint8_t  enable;
};

struct msft_rsp_le_monitor_adv_enable {
	uint8_t  status;
	uint8_t  subcmd;
};

#define MSFT_SUBCMD_READ_ABS_RSSI		0x06

struct msft_cmd_read_abs_rssi {
	uint8_t  subcmd;
	uint16_t handle;
};

struct msft_rsp_read_abs_rssi {
	uint8_t  status;
	uint8_t  subcmd;
	uint16_t handle;
	uint8_t  rssi;
};

struct vendor_ocf;
struct vendor_evt;

const struct vendor_ocf *msft_vendor_ocf(void);
const struct vendor_evt *msft_vendor_evt(void);
