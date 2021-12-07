/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2017  Intel Corporation. All rights reserved.
 *
 *
 */
#define GENERIC_LEVEL_SERVER_MODEL_ID	0x1002
#define GENERIC_LEVEL_CLIENT_MODEL_ID	0x1003
#define OP_GENERIC_LEVEL_GET			0x8205
#define OP_GENERIC_LEVEL_SET			0x8206
#define OP_GENERIC_LEVEL_SET_UNACK		0x8207
#define OP_GENERIC_LEVEL_STATUS			0x8208
#define OP_GENERIC_DELTA_SET			0x8209
#define OP_GENERIC_DELTA_SET_UNACK		0x820A
#define OP_GENERIC_MOVE_SET			0x820B
#define OP_GENERIC_MOVE_SET_UNACK		0x820C
void level_set_node(const char *args);
bool level_client_init(uint8_t ele);
