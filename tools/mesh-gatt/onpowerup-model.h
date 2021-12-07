/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2017  Intel Corporation. All rights reserved.
 *
 *
 */

#define GENERIC_POWER_ONOFF_SERVER_MODEL_ID         0x1006
#define GENERIC_POWER_ONOFF_SETUP_SERVER_MODEL_ID   0x1007
#define GENERIC_POWER_ONOFF_CLIENT_MODEL_ID         0x1008

#define OP_GENERIC_POWER_ONOFF_GET			0x8211
#define OP_GENERIC_POWER_ONOFF_STATUS			0x8212
#define OP_GENERIC_POWER_ONOFF_SET			0x8213
#define OP_GENERIC_POWER_ONOFF_SET_UNACK		0x8214

void power_onoff_set_node(const char *args);
bool power_onoff_client_init(uint8_t ele);
