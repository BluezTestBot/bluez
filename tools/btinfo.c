/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2011-2012  Intel Corporation
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "monitor/mainloop.h"
#include "monitor/bt.h"
#include "src/shared/util.h"
#include "src/shared/hci.h"

#define le16_to_cpu(val) (val)
#define le32_to_cpu(val) (val)
#define cpu_to_le16(val) (val)
#define cpu_to_le32(val) (val)

static struct bt_hci *hci_dev;
static bool reset_required = false;

static void shutdown_timeout(int id, void *user_data)
{
	mainloop_remove_timeout(id);

	mainloop_quit();
}

static void shutdown_complete(const void *data, uint8_t size, void *user_data)
{
	unsigned int id = PTR_TO_UINT(user_data);

	shutdown_timeout(id, NULL);
}

static void shutdown_device(void)
{
	unsigned int id;

	bt_hci_flush(hci_dev);

	if (reset_required) {
		id = mainloop_add_timeout(5, shutdown_timeout, NULL, NULL);

		bt_hci_send(hci_dev, BT_HCI_CMD_RESET, NULL, 0,
				shutdown_complete, UINT_TO_PTR(id), NULL);
	} else
		mainloop_quit();
}

static void local_features_callback(const void *data, uint8_t size,
							void *user_data)
{
	shutdown_device();
}

static bool cmd_local(int argc, char *argv[])
{
	if (reset_required)
		bt_hci_send(hci_dev, BT_HCI_CMD_RESET, NULL, 0,
						NULL, NULL, NULL);

	bt_hci_send(hci_dev, BT_HCI_CMD_READ_LOCAL_FEATURES, NULL, 0,
					local_features_callback, NULL, NULL);

	return true;
}

typedef bool (*cmd_func_t)(int argc, char *argv[]);

static const struct {
	const char *name;
	cmd_func_t func;
	const char *help;
} cmd_table[] = {
	{ "local", cmd_local, "Print local controller details" },
	{ }
};

static void signal_callback(int signum, void *user_data)
{
	static bool terminated = false;

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		if (!terminated) {
			shutdown_device();
			terminated = true;
		}
		break;
	}
}

static void usage(void)
{
	int i;

	printf("btinfo - Bluetooth device testing tool\n"
		"Usage:\n");
	printf("\tbtinfo [options] <command>\n");
	printf("options:\n"
		"\t-i, --index <num>      Use specified controller\n"
		"\t-h, --help             Show help options\n");
	printf("commands:\n");
	for (i = 0; cmd_table[i].name; i++)
		printf("\t%-25s%s\n", cmd_table[i].name, cmd_table[i].help);
}

static const struct option main_options[] = {
	{ "index",   required_argument, NULL, 'i' },
	{ "version", no_argument,       NULL, 'v' },
	{ "help",    no_argument,       NULL, 'h' },
	{ }
};

int main(int argc, char *argv[])
{
	cmd_func_t func = NULL;
	uint16_t index = 0;
	const char *str;
	sigset_t mask;
	int i, exit_status;

	for (;;) {
		int opt;

		opt = getopt_long(argc, argv, "i:vh", main_options, NULL);
		if (opt < 0)
			break;

		switch (opt) {
		case 'i':
			if (strlen(optarg) > 3 && !strncmp(optarg, "hci", 3))
				str = optarg + 3;
			else
				str = optarg;
			if (!isdigit(*str)) {
				usage();
				return EXIT_FAILURE;
			}
			index = atoi(str);
			break;
		case 'v':
			printf("%s\n", VERSION);
			return EXIT_SUCCESS;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		default:
			return EXIT_FAILURE;
		}
	}

	if (argc - optind < 1) {
		fprintf(stderr, "Missing command argument\n");
		return EXIT_FAILURE;
	}

	for (i = 0; cmd_table[i].name; i++) {
		if (!strcmp(cmd_table[i].name, argv[optind])) {
			func = cmd_table[i].func;
			break;
		}
	}

	if (!func) {
		fprintf(stderr, "Unsupported command specified\n");
		return EXIT_FAILURE;
	}

	mainloop_init();

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	mainloop_set_signal(&mask, signal_callback, NULL, NULL);

	printf("3D Synchronization Profile testing ver %s\n", VERSION);

	hci_dev = bt_hci_new_user_channel(index);
	if (!hci_dev) {
		fprintf(stderr, "Failed to open HCI user channel\n");
		return EXIT_FAILURE;
	}

	reset_required = true;

	if (!func(argc - optind - 1, argv + optind + 1)) {
		bt_hci_unref(hci_dev);
		return EXIT_FAILURE;
	}

	exit_status = mainloop_run();

	bt_hci_unref(hci_dev);

	return exit_status;
}
