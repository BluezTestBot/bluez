// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2017  Intel Corporation. All rights reserved.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <wordexp.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <glib.h>

#include "src/shared/shell.h"
#include "src/shared/util.h"

#include "tools/mesh-gatt/mesh-net.h"
#include "tools/mesh-gatt/keys.h"
#include "tools/mesh-gatt/net.h"
#include "tools/mesh-gatt/node.h"
#include "tools/mesh-gatt/prov-db.h"
#include "tools/mesh-gatt/util.h"
#include "tools/mesh-gatt/onpowerup-model.h"

static uint8_t trans_id;
static uint16_t power_onoff_app_idx = APP_IDX_INVALID;

static int client_bind(uint16_t app_idx, int action)
{
	if (action == ACTION_ADD) {
                if (power_onoff_app_idx != APP_IDX_INVALID) {
			return MESH_STATUS_INSUFF_RESOURCES;
		} else {
                        power_onoff_app_idx = app_idx;
                        bt_shell_printf("OnPowerUp client model: new binding"
					" %4.4x\n", app_idx);
		}
	} else {
                if (power_onoff_app_idx == app_idx)
                        power_onoff_app_idx = APP_IDX_INVALID;
	}
	return MESH_STATUS_SUCCESS;
}

static bool client_msg_recvd(uint16_t src, uint8_t *data,
				uint16_t len, void *user_data)
{
	uint32_t opcode;
	int n;
        char s[10];

	if (mesh_opcode_get(data, len, &opcode, &n)) {
		len -= n;
		data += n;
	} else
		return false;

	switch (opcode) {
	default:
		return false;

        case OP_GENERIC_POWER_ONOFF_STATUS:
                bt_shell_printf("OnPowerUp Model Message received (%d) opcode %x\n",
                                                                        len, opcode);
                print_byte_array("\t",data, len);
                if (len != 1)
                        break;
                if(data[0] == 0){
                    sprintf(s, "%s", "OFF");
                }else if(data[0] == 1){
                    sprintf(s, "%s", "ON");
                }else if(data[0] == 2){
                    sprintf(s, "%s", "RESUME");
                }else{
                    sprintf(s, "%s", "?UNKNOWN");
                }
                bt_shell_printf("Node %4.4x: OnPowerUp Status present = %s\n", src, s);
                break;
	}
	return true;
}


static uint32_t target;
static uint32_t parms[8];

static uint32_t read_input_parameters(int argc, char *argv[])
{
	uint32_t i;

	if (!argc)
		return 0;

	--argc;
	++argv;

	if (!argc || argv[0][0] == '\0')
		return 0;

	memset(parms, 0xff, sizeof(parms));

	for (i = 0; i < sizeof(parms)/sizeof(parms[0]) && i < (unsigned) argc;
									i++) {
		sscanf(argv[i], "%x", &parms[i]);
		if (parms[i] == 0xffffffff)
			break;
	}

	return i;
}

static void cmd_set_node(int argc, char *argv[])
{
	uint32_t dst;
	char *end;

	dst = strtol(argv[1], &end, 16);
	if (end != (argv[1] + 4)) {
		bt_shell_printf("Bad unicast address %s: "
				"expected format 4 digit hex\n", argv[1]);
		target = UNASSIGNED_ADDRESS;
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	} else {
                bt_shell_printf("Controlling OnPowerUp for node %4.4x\n", dst);
		target = dst;
                set_menu_prompt("OnPowerUp", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}
}

static bool send_cmd(uint8_t *buf, uint16_t len)
{
	struct mesh_node *node = node_get_local_node();
	uint8_t ttl;

	if(!node)
		return false;

	ttl = node_get_default_ttl(node);

	return net_access_layer_send(ttl, node_get_primary(node),
                                        target, power_onoff_app_idx, buf, len);
}

static void cmd_get_status(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	struct mesh_node *node;

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	node = node_find_by_addr(target);

        if (!node){
                bt_shell_printf("Warning: node %4.4x not found in database\n",target);
        }

        n = mesh_opcode_set(OP_GENERIC_POWER_ONOFF_GET, msg);

	if (!send_cmd(msg, n)) {
                bt_shell_printf("Failed to send \"GENERIC POWER ONOFF GET\"\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	struct mesh_node *node;

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	node = node_find_by_addr(target);

        if (!node){
                bt_shell_printf("Warning: node %4.4x not found in database\n",target);
        }

	if ((read_input_parameters(argc, argv) != 1) &&
                                        parms[0] != 0 && parms[0] != 1 && parms[0] != 2) {
                bt_shell_printf("Bad arguments: Expecting \"0\" or \"1\" or \"2\"\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

        n = mesh_opcode_set(OP_GENERIC_POWER_ONOFF_SET, msg);
	msg[n++] = parms[0];
	msg[n++] = trans_id++;

	if (!send_cmd(msg, n)) {
                bt_shell_printf("Failed to send \"GENERIC POWER ONOFF SET\"\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static const struct bt_shell_menu power_onoff_menu = {
        .name = "power_onoff",
        .desc = "Power OnOff (OnPowerUp) Model Submenu",
	.entries = {
	{"target",		"<unicast>",			cmd_set_node,
						"Set node to configure"},
	{"get",			NULL,				cmd_get_status,
                                                "Get OnPowerUp status"},
        {"set",                 "<0/1/2>",			cmd_set,
                                                "Set OnPowerUp status (OFF/ON/RESTORE)"},
	{} },
};

static struct mesh_model_ops client_cbs = {
	client_msg_recvd,
	client_bind,
	NULL,
	NULL
};

bool power_onoff_client_init(uint8_t ele)
{
        if (!node_local_model_register(ele, GENERIC_POWER_ONOFF_CLIENT_MODEL_ID,
					&client_cbs, NULL))
		return false;

        bt_shell_add_submenu(&power_onoff_menu);

	return true;
}
