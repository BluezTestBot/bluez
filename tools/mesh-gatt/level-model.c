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
#include "tools/mesh-gatt/level-model.h"

static uint8_t trans_id;
static uint16_t level_app_idx = APP_IDX_INVALID;
static int client_bind(uint16_t app_idx, int action)
{
        if (action == ACTION_ADD) {
                if (level_app_idx != APP_IDX_INVALID) {
                        return MESH_STATUS_INSUFF_RESOURCES;
                } else {
                        level_app_idx = app_idx;
                        bt_shell_printf("Level client model: new binding"
                                        " %4.4x\n", app_idx);
                }
        } else {
                if (level_app_idx == app_idx)
                        level_app_idx = APP_IDX_INVALID;
        }
        return MESH_STATUS_SUCCESS;
}
static void print_remaining_time(uint8_t remaining_time)
{
        uint8_t step = (remaining_time & 0xc0) >> 6;
        uint8_t count = remaining_time & 0x3f;
        int secs = 0, msecs = 0, minutes = 0, hours = 0;
        switch (step) {
        case 0:
                msecs = 100 * count;
                secs = msecs / 1000;
                msecs -= (secs * 1000);
                break;
        case 1:
                secs = 1 * count;
                minutes = secs / 60;
                secs -= (minutes * 60);
                break;
        case 2:
                secs = 10 * count;
                minutes = secs / 60;
                secs -= (minutes * 60);
                break;
        case 3:
                minutes = 10 * count;
                hours = minutes / 60;
                minutes -= (hours * 60);
                break;
        default:
                break;
        }
        bt_shell_printf("\n\t\tRemaining time: %d hrs %d mins %d secs %d"
                        " msecs\n", hours, minutes, secs, msecs);
}
static bool client_msg_recvd(uint16_t src, uint8_t *data,
                             uint16_t len, void *user_data)
{
        uint32_t opcode;
        int n;
        uint8_t *p;
        int16_t lev;
        char s[128];

        if (mesh_opcode_get(data, len, &opcode, &n)) {
                len -= n;
                data += n;
        } else
                return false;

        switch (opcode) {
        default:
                return false;
        case OP_GENERIC_LEVEL_STATUS:
                bt_shell_printf("Level Model Message received (%d) opcode %x\n",
                                len, opcode);
                print_byte_array("\t",data, len);

                if (len != 2 && len != 4 && len != 5)
                        break;
                lev = 0;
                p = (uint8_t *)&lev;
#if __BYTE_ORDER == __LITTLE_ENDIAN
                p[0] = data[0];
                p[1] = data[1];
#elif __BYTE_ORDER == __BIG_ENDIAN
                p[1] = data[0];
                p[0] = data[1];
#else
#error "Unknown byte order"
#error Processor endianness unknown!
#endif
                sprintf(s, "Node %4.4x: Level Status present = %d",
                                src, lev);
                if (len >= 4) {
                        lev = (int16_t)(((uint16_t)data[3] << 8) |  (uint16_t)data[2]);
                        sprintf(s, ", target = %d",
                                        lev);
                }
                bt_shell_printf("%s\n", s);
                if(len == 5){
                        print_remaining_time(data[4]);
                }
                break;
        }
        return true;
}
static uint32_t target;
static int32_t parms[8];
static uint32_t read_input_parameters(int argc, char *argv[])
{
        uint32_t i;
        if (!argc)
                return 0;
        --argc;
        ++argv;
        if (!argc || argv[0][0] == '\0')
                return 0;
        for (i = 0; i < sizeof(parms)/sizeof(parms[0]) && i < (unsigned) argc;
             i++) {
                if(sscanf(argv[i], "%d", &parms[i]) <= 0)
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
                bt_shell_printf("Controlling Level for node %4.4x\n", dst);
                target = dst;
                set_menu_prompt("Level", argv[1]);
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
                                     target, level_app_idx, buf, len);
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

        n = mesh_opcode_set(OP_GENERIC_LEVEL_GET, msg);
        if (!send_cmd(msg, n)) {
                bt_shell_printf("Failed to send \"GENERIC LEVEL GET\"\n");
                return bt_shell_noninteractive_quit(EXIT_FAILURE);
        }
        return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}
static void cmd_set(int argc, char *argv[])
{
        uint16_t n;
        uint8_t msg[32];
        struct mesh_node *node;
        uint8_t *p;
        int np;
        uint32_t opcode;
        int16_t level;

        if (IS_UNASSIGNED(target)) {
                bt_shell_printf("Destination not set\n");
                return bt_shell_noninteractive_quit(EXIT_FAILURE);
        }
        node = node_find_by_addr(target);

        if (!node){
                bt_shell_printf("Warning: node %4.4x not found in database\n",target);
        }

        np = read_input_parameters(argc, argv);
        if ((np != 1) && (np != 2) &&
                        parms[0] < -32768 && parms[0] > 32767 &&
                        parms[1] != 0 && parms[1] != 1) {
                bt_shell_printf("Bad arguments: Expecting an integer "
                                "-32768 to 32767 and an optional 0 or 1 as unack\n");
                return bt_shell_noninteractive_quit(EXIT_FAILURE);
        }

        if( (np==2) && parms[1] ){
                opcode = OP_GENERIC_LEVEL_SET_UNACK;
        }else{
                opcode = OP_GENERIC_LEVEL_SET;
        }

        n = mesh_opcode_set(opcode, msg);
        level = (int16_t)parms[0];
        p = (uint8_t *)&level;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        msg[n++] = p[0];
        msg[n++] = p[1];
#elif __BYTE_ORDER == __BIG_ENDIAN
        msg[n++] = p[1];
        msg[n++] = p[0];
#else
#error "Unknown byte order"
#error Processor endianness unknown!
#endif
        msg[n++] = trans_id++;
        if (!send_cmd(msg, n)) {
                bt_shell_printf("Failed to send \"GENERIC LEVEL SET\"\n");
                return bt_shell_noninteractive_quit(EXIT_FAILURE);
        }
        return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}
static const struct bt_shell_menu level_menu = {
        .name = "level",
        .desc = "Level Model Submenu",
        .entries = {
                {"target",		"<unicast>",			cmd_set_node,
                 "Set node to configure"},
                {"get",			NULL,				cmd_get_status,
                 "Get Level status"},
                {"level",		"<-32768/+32767> [unack]",	cmd_set,
                 "Send \"SET Level\" command"},
                {} },
};
static struct mesh_model_ops client_cbs = {
        client_msg_recvd,
        client_bind,
        NULL,
        NULL
};
bool level_client_init(uint8_t ele)
{
        if (!node_local_model_register(ele, GENERIC_LEVEL_CLIENT_MODEL_ID,
                                       &client_cbs, NULL))
                return false;
        bt_shell_add_submenu(&level_menu);
        return true;
}
