// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2020  Intel Corporation. All rights reserved.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <wordexp.h>

#include <glib.h>

#include "gdbus/gdbus.h"

#include "lib/bluetooth.h"
#include "lib/uuid.h"

#include "profiles/audio/a2dp-codecs.h"

#include "src/shared/util.h"
#include "src/shared/shell.h"
#include "src/shared/io.h"
#include "player.h"

/* String display constants */
#define COLORED_NEW	COLOR_GREEN "NEW" COLOR_OFF
#define COLORED_CHG	COLOR_YELLOW "CHG" COLOR_OFF
#define COLORED_DEL	COLOR_RED "DEL" COLOR_OFF

#define BLUEZ_MEDIA_INTERFACE "org.bluez.Media1"
#define BLUEZ_MEDIA_PLAYER_INTERFACE "org.bluez.MediaPlayer1"
#define BLUEZ_MEDIA_FOLDER_INTERFACE "org.bluez.MediaFolder1"
#define BLUEZ_MEDIA_ITEM_INTERFACE "org.bluez.MediaItem1"
#define BLUEZ_MEDIA_ENDPOINT_INTERFACE "org.bluez.MediaEndpoint1"

#define BLUEZ_MEDIA_ENDPOINT_PATH "/local/endpoint"

#define NSEC_USEC(_t) (_t / 1000L)
#define SEC_USEC(_t)  (_t  * 1000000L)
#define TS_USEC(_ts)  (SEC_USEC((_ts)->tv_sec) + NSEC_USEC((_ts)->tv_nsec))

struct endpoint {
	char *path;
	char *uuid;
	uint8_t codec;
	struct iovec *caps;
	bool auto_accept;
	char *transport;
};

static DBusConnection *dbus_conn;
static GDBusProxy *default_player;
static GList *medias = NULL;
static GList *players = NULL;
static GList *folders = NULL;
static GList *items = NULL;
static GList *endpoints = NULL;
static GList *local_endpoints = NULL;

static void endpoint_unregister(void *data)
{
	struct endpoint *ep = data;

	bt_shell_printf("Endpoint %s unregistered\n", ep->path);
	g_dbus_unregister_interface(dbus_conn, ep->path,
						BLUEZ_MEDIA_ENDPOINT_INTERFACE);
}

static void disconnect_handler(DBusConnection *connection, void *user_data)
{
	g_list_free_full(local_endpoints, endpoint_unregister);
	local_endpoints = NULL;
}

static bool check_default_player(void)
{
	if (!default_player) {
		bt_shell_printf("No default player available\n");
		return FALSE;
	}

	return TRUE;
}

static char *generic_generator(const char *text, int state, GList *source)
{
	static int index = 0;

	if (!state)
		index = 0;

	return g_dbus_proxy_path_lookup(source, &index, text);
}

static char *player_generator(const char *text, int state)
{
	return generic_generator(text, state, players);
}

static char *item_generator(const char *text, int state)
{
	return generic_generator(text, state, items);
}

static void play_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to play: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Play successful\n");

	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static void cmd_play(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (argc > 1) {
		proxy = g_dbus_proxy_lookup(items, NULL, argv[1],
						BLUEZ_MEDIA_ITEM_INTERFACE);
		if (proxy == NULL) {
			bt_shell_printf("Item %s not available\n", argv[1]);
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
	} else {
		if (!check_default_player())
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		proxy = default_player;
	}

	if (g_dbus_proxy_method_call(proxy, "Play", NULL, play_reply,
							NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to play\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to play %s\n", argv[1] ? : "");
}

static void pause_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to pause: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Pause successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_pause(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "Pause", NULL,
					pause_reply, NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to play\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to pause\n");
}

static void stop_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to stop: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Stop successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_stop(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "Stop", NULL, stop_reply,
							NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to stop\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to stop\n");
}

static void next_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to jump to next: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Next successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_next(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "Next", NULL, next_reply,
							NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to jump to next\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to jump to next\n");
}

static void previous_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to jump to previous: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Previous successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_previous(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "Previous", NULL,
					previous_reply, NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to jump to previous\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to jump to previous\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void fast_forward_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to fast forward: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("FastForward successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_fast_forward(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "FastForward", NULL,
				fast_forward_reply, NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to jump to previous\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Fast forward playback\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void rewind_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to rewind: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Rewind successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_rewind(int argc, char *argv[])
{
	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (g_dbus_proxy_method_call(default_player, "Rewind", NULL,
					rewind_reply, NULL, NULL) == FALSE) {
		bt_shell_printf("Failed to rewind\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Rewind playback\n");
}

static void generic_callback(const DBusError *error, void *user_data)
{
	char *str = user_data;

	if (dbus_error_is_set(error)) {
		bt_shell_printf("Failed to set %s: %s\n", str, error->name);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	} else {
		bt_shell_printf("Changing %s succeeded\n", str);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}
}

static void cmd_equalizer(int argc, char *argv[])
{
	char *value;
	DBusMessageIter iter;

	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!g_dbus_proxy_get_property(default_player, "Equalizer", &iter)) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	value = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_player, "Equalizer",
						DBUS_TYPE_STRING, &value,
						generic_callback, value,
						g_free) == FALSE) {
		bt_shell_printf("Failed to setting equalizer\n");
		g_free(value);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to set equalizer\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_repeat(int argc, char *argv[])
{
	char *value;
	DBusMessageIter iter;

	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!g_dbus_proxy_get_property(default_player, "Repeat", &iter)) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	value = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_player, "Repeat",
						DBUS_TYPE_STRING, &value,
						generic_callback, value,
						g_free) == FALSE) {
		bt_shell_printf("Failed to set repeat\n");
		g_free(value);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to set repeat\n");
}

static void cmd_shuffle(int argc, char *argv[])
{
	char *value;
	DBusMessageIter iter;

	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!g_dbus_proxy_get_property(default_player, "Shuffle", &iter)) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	value = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_player, "Shuffle",
						DBUS_TYPE_STRING, &value,
						generic_callback, value,
						g_free) == FALSE) {
		bt_shell_printf("Failed to set shuffle\n");
		g_free(value);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to set shuffle\n");
}

static void cmd_scan(int argc, char *argv[])
{
	char *value;
	DBusMessageIter iter;

	if (!check_default_player())
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (!g_dbus_proxy_get_property(default_player, "Shuffle", &iter)) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	value = g_strdup(argv[1]);

	if (g_dbus_proxy_set_property_basic(default_player, "Shuffle",
						DBUS_TYPE_STRING, &value,
						generic_callback, value,
						g_free) == FALSE) {
		bt_shell_printf("Failed to set scan\n");
		g_free(value);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to set scan\n");
}

static char *proxy_description(GDBusProxy *proxy, const char *title,
						const char *description)
{
	const char *path;

	path = g_dbus_proxy_get_path(proxy);

	return g_strdup_printf("%s%s%s%s %s ",
					description ? "[" : "",
					description ? : "",
					description ? "] " : "",
					title, path);
}

static void print_media(GDBusProxy *proxy, const char *description)
{
	char *str;

	str = proxy_description(proxy, "Media", description);

	bt_shell_printf("%s\n", str);

	g_free(str);
}

static void print_player(GDBusProxy *proxy, const char *description)
{
	char *str;

	str = proxy_description(proxy, "Player", description);

	bt_shell_printf("%s%s\n", str,
			default_player == proxy ? "[default]" : "");

	g_free(str);
}

static void cmd_list(int argc, char *arg[])
{
	GList *l;

	for (l = players; l; l = g_list_next(l)) {
		GDBusProxy *proxy = l->data;
		print_player(proxy, NULL);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	const char *valstr;
	DBusMessageIter subiter;

	if (iter == NULL) {
		bt_shell_printf("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		bt_shell_printf("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		bt_shell_printf("%s%s: %s\n", label, name, valstr);
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		bt_shell_printf("%s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		bt_shell_printf("%s%s: 0x%06x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		bt_shell_printf("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		bt_shell_printf("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);
		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		dbus_message_iter_get_basic(&subiter, &valstr);
		dbus_message_iter_next(&subiter);
		print_iter(label, valstr, &subiter);
		break;
	default:
		bt_shell_printf("%s%s has unsupported type\n", label, name);
		break;
	}
}

static void print_property(GDBusProxy *proxy, const char *name)
{
	DBusMessageIter iter;

	if (g_dbus_proxy_get_property(proxy, name, &iter) == FALSE)
		return;

	print_iter("\t", name, &iter);
}

static void cmd_show_item(int argc, char *argv[])
{
	GDBusProxy *proxy;

	proxy = g_dbus_proxy_lookup(items, NULL, argv[1],
						BLUEZ_MEDIA_ITEM_INTERFACE);
	if (!proxy) {
		bt_shell_printf("Item %s not available\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	bt_shell_printf("Item %s\n", g_dbus_proxy_get_path(proxy));

	print_property(proxy, "Player");
	print_property(proxy, "Name");
	print_property(proxy, "Type");
	print_property(proxy, "FolderType");
	print_property(proxy, "Playable");
	print_property(proxy, "Metadata");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_show(int argc, char *argv[])
{
	GDBusProxy *proxy;
	GDBusProxy *folder;
	GDBusProxy *item;
	DBusMessageIter iter;
	const char *path;

	if (argc < 2) {
		if (check_default_player() == FALSE)
			return bt_shell_noninteractive_quit(EXIT_FAILURE);

		proxy = default_player;
	} else {
		proxy = g_dbus_proxy_lookup(players, NULL, argv[1],
						BLUEZ_MEDIA_PLAYER_INTERFACE);
		if (!proxy) {
			bt_shell_printf("Player %s not available\n", argv[1]);
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
	}

	bt_shell_printf("Player %s\n", g_dbus_proxy_get_path(proxy));

	print_property(proxy, "Name");
	print_property(proxy, "Repeat");
	print_property(proxy, "Equalizer");
	print_property(proxy, "Shuffle");
	print_property(proxy, "Scan");
	print_property(proxy, "Status");
	print_property(proxy, "Position");
	print_property(proxy, "Track");

	folder = g_dbus_proxy_lookup(folders, NULL,
					g_dbus_proxy_get_path(proxy),
					BLUEZ_MEDIA_FOLDER_INTERFACE);
	if (folder == NULL)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	bt_shell_printf("Folder %s\n", g_dbus_proxy_get_path(proxy));

	print_property(folder, "Name");
	print_property(folder, "NumberOfItems");

	if (!g_dbus_proxy_get_property(proxy, "Playlist", &iter))
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	dbus_message_iter_get_basic(&iter, &path);

	item = g_dbus_proxy_lookup(items, NULL, path,
					BLUEZ_MEDIA_ITEM_INTERFACE);
	if (item == NULL)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	bt_shell_printf("Playlist %s\n", path);

	print_property(item, "Name");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_select(int argc, char *argv[])
{
	GDBusProxy *proxy;

	proxy = g_dbus_proxy_lookup(players, NULL, argv[1],
						BLUEZ_MEDIA_PLAYER_INTERFACE);
	if (proxy == NULL) {
		bt_shell_printf("Player %s not available\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (default_player == proxy)
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);

	default_player = proxy;
	print_player(proxy, NULL);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void change_folder_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to change folder: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("ChangeFolder successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void change_folder_setup(DBusMessageIter *iter, void *user_data)
{
	const char *path = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &path);
}

static void cmd_change_folder(int argc, char *argv[])
{
	GDBusProxy *proxy;

	if (dbus_validate_path(argv[1], NULL) == FALSE) {
		bt_shell_printf("Not a valid path\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (check_default_player() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	proxy = g_dbus_proxy_lookup(folders, NULL,
					g_dbus_proxy_get_path(default_player),
					BLUEZ_MEDIA_FOLDER_INTERFACE);
	if (proxy == NULL) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (g_dbus_proxy_method_call(proxy, "ChangeFolder", change_folder_setup,
				change_folder_reply, argv[1], NULL) == FALSE) {
		bt_shell_printf("Failed to change current folder\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to change folder\n");
}

struct list_items_args {
	int start;
	int end;
};

static void list_items_setup(DBusMessageIter *iter, void *user_data)
{
	struct list_items_args *args = user_data;
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					&dict);

	if (args->start < 0)
		goto done;

	g_dbus_dict_append_entry(&dict, "Start",
					DBUS_TYPE_UINT32, &args->start);

	if (args->end < 0)
		goto done;

	g_dbus_dict_append_entry(&dict, "End", DBUS_TYPE_UINT32, &args->end);

done:
	dbus_message_iter_close_container(iter, &dict);
}

static void list_items_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to list items: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("ListItems successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_list_items(int argc, char *argv[])
{
	GDBusProxy *proxy;
	struct list_items_args *args;

	if (check_default_player() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	proxy = g_dbus_proxy_lookup(folders, NULL,
					g_dbus_proxy_get_path(default_player),
					BLUEZ_MEDIA_FOLDER_INTERFACE);
	if (proxy == NULL) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	args = g_new0(struct list_items_args, 1);
	args->start = -1;
	args->end = -1;

	if (argc < 2)
		goto done;

	errno = 0;
	args->start = strtol(argv[1], NULL, 10);
	if (errno != 0) {
		bt_shell_printf("%s(%d)\n", strerror(errno), errno);
		g_free(args);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (argc < 3)
		goto done;

	errno = 0;
	args->end = strtol(argv[2], NULL, 10);
	if (errno != 0) {
		bt_shell_printf("%s(%d)\n", strerror(errno), errno);
		g_free(args);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

done:
	if (g_dbus_proxy_method_call(proxy, "ListItems", list_items_setup,
				list_items_reply, args, g_free) == FALSE) {
		bt_shell_printf("Failed to change current folder\n");
		g_free(args);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to list items\n");
}

static void search_setup(DBusMessageIter *iter, void *user_data)
{
	char *string = user_data;
	DBusMessageIter dict;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &string);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
					DBUS_TYPE_STRING_AS_STRING
					DBUS_TYPE_VARIANT_AS_STRING
					DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
					&dict);

	dbus_message_iter_close_container(iter, &dict);
}

static void search_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to search: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Search successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_search(int argc, char *argv[])
{
	GDBusProxy *proxy;
	char *string;

	if (check_default_player() == FALSE)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	proxy = g_dbus_proxy_lookup(folders, NULL,
					g_dbus_proxy_get_path(default_player),
					BLUEZ_MEDIA_FOLDER_INTERFACE);
	if (proxy == NULL) {
		bt_shell_printf("Operation not supported\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	string = g_strdup(argv[1]);

	if (g_dbus_proxy_method_call(proxy, "Search", search_setup,
				search_reply, string, g_free) == FALSE) {
		bt_shell_printf("Failed to search\n");
		g_free(string);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to search\n");
}

static void add_to_nowplaying_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		bt_shell_printf("Failed to queue: %s\n", error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("AddToNowPlaying successful\n");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_queue(int argc, char *argv[])
{
	GDBusProxy *proxy;

	proxy = g_dbus_proxy_lookup(items, NULL, argv[1],
						BLUEZ_MEDIA_ITEM_INTERFACE);
	if (proxy == NULL) {
		bt_shell_printf("Item %s not available\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (g_dbus_proxy_method_call(proxy, "AddtoNowPlaying", NULL,
					add_to_nowplaying_reply, NULL,
					NULL) == FALSE) {
		bt_shell_printf("Failed to play\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Attempting to queue %s\n", argv[1]);
}

static const struct bt_shell_menu player_menu = {
	.name = "player",
	.desc = "Media Player Submenu",
	.entries = {
	{ "list",         NULL,       cmd_list, "List available players" },
	{ "show",         "[player]", cmd_show, "Player information",
							player_generator},
	{ "select",       "<player>", cmd_select, "Select default player",
							player_generator},
	{ "play",         "[item]",   cmd_play, "Start playback",
							item_generator},
	{ "pause",        NULL,       cmd_pause, "Pause playback" },
	{ "stop",         NULL,       cmd_stop, "Stop playback" },
	{ "next",         NULL,       cmd_next, "Jump to next item" },
	{ "previous",     NULL,       cmd_previous, "Jump to previous item" },
	{ "fast-forward", NULL,       cmd_fast_forward,
						"Fast forward playback" },
	{ "rewind",       NULL,       cmd_rewind, "Rewind playback" },
	{ "equalizer",    "<on/off>", cmd_equalizer,
						"Enable/Disable equalizer"},
	{ "repeat",       "<singletrack/alltrack/group/off>", cmd_repeat,
						"Set repeat mode"},
	{ "shuffle",      "<alltracks/group/off>", cmd_shuffle,
						"Set shuffle mode"},
	{ "scan",         "<alltracks/group/off>", cmd_scan,
						"Set scan mode"},
	{ "change-folder", "<item>",  cmd_change_folder,
						"Change current folder",
							item_generator},
	{ "list-items", "[start] [end]",  cmd_list_items,
					"List items of current folder" },
	{ "search",     "<string>",   cmd_search,
					"Search items containing string" },
	{ "queue",       "<item>",    cmd_queue, "Add item to playlist queue",
							item_generator},
	{ "show-item",   "<item>",    cmd_show_item, "Show item information",
							item_generator},
	{} },
};

static char *endpoint_generator(const char *text, int state)
{
	return generic_generator(text, state, endpoints);
}

static char *local_endpoint_generator(const char *text, int state)
{
	return generic_generator(text, state, local_endpoints);
}

static void print_endpoint(void *data, void *user_data)
{
	GDBusProxy *proxy = data;
	const char *description = user_data;
	char *str;

	str = proxy_description(proxy, "Endpoint", description);

	bt_shell_printf("%s\n", str);

	g_free(str);
}

static void cmd_list_endpoints(int argc, char *argv[])
{
	GList *l;

	if (argc > 1) {
		if (strcmp("local", argv[1])) {
			bt_shell_printf("Endpoint list %s not available\n",
					argv[1]);
			return bt_shell_noninteractive_quit(EXIT_SUCCESS);
		}

		for (l = local_endpoints; l; l = g_list_next(l)) {
			struct endpoint *ep = l->data;

			bt_shell_printf("Endpoint %s\n", ep->path);
		}

		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	for (l = endpoints; l; l = g_list_next(l)) {
		GDBusProxy *proxy = l->data;
		print_endpoint(proxy, NULL);
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void confirm_response(const char *input, void *user_data)
{
	DBusMessage *msg = user_data;

	if (!strcasecmp(input, "y") || !strcasecmp(input, "yes"))
		g_dbus_send_reply(dbus_conn, msg, DBUS_TYPE_INVALID);
	else if (!strcasecmp(input, "n") || !strcmp(input, "no"))
		g_dbus_send_error(dbus_conn, msg, "org.bluez.Error.Rejected",
									NULL);
	else
		g_dbus_send_error(dbus_conn, msg, "org.bluez.Error.Canceled",
									NULL);
}

static DBusMessage *endpoint_set_configuration(DBusConnection *conn,
					DBusMessage *msg, void *user_data)
{
	struct endpoint *ep = user_data;
	DBusMessageIter args, props;
	const char *path;

	dbus_message_iter_init(msg, &args);

	dbus_message_iter_get_basic(&args, &path);
	dbus_message_iter_next(&args);

	dbus_message_iter_recurse(&args, &props);
	if (dbus_message_iter_get_arg_type(&props) != DBUS_TYPE_DICT_ENTRY)
		return g_dbus_create_error(msg,
					 "org.bluez.Error.InvalidArguments",
					 NULL);

	bt_shell_printf("Endpoint: SetConfiguration\n");
	bt_shell_printf("\tTransport %s\n", path);
	print_iter("\t", "Properties", &props);

	free(ep->transport);
	ep->transport = strdup(path);

	if (ep->auto_accept) {
		bt_shell_printf("Auto Accepting...\n");
		return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
	}

	bt_shell_prompt_input("Endpoint", "Accept (yes/no):", confirm_response,
							dbus_message_ref(msg));

	return NULL;
}

static DBusMessage *endpoint_clear_configuration(DBusConnection *conn,
					DBusMessage *msg, void *user_data)
{
	struct endpoint *ep = user_data;

	free(ep->transport);
	ep->transport = NULL;

	return g_dbus_create_reply(msg, DBUS_TYPE_INVALID);
}

static struct endpoint *endpoint_find(const char *pattern)
{
	GList *l;

	for (l = local_endpoints; l; l = g_list_next(l)) {
		struct endpoint *ep = l->data;

		/* match object path */
		if (!strcmp(ep->path, pattern))
			return ep;

		/* match UUID */
		if (!strcmp(ep->uuid, pattern))
			return ep;
	}

	return NULL;
}

static void cmd_show_endpoint(int argc, char *argv[])
{
	GDBusProxy *proxy;

	proxy = g_dbus_proxy_lookup(endpoints, NULL, argv[1],
						BLUEZ_MEDIA_ENDPOINT_INTERFACE);
	if (!proxy) {
		bt_shell_printf("Endpoint %s not found\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_SUCCESS);
	}

	bt_shell_printf("Endpoint %s\n", g_dbus_proxy_get_path(proxy));

	print_property(proxy, "UUID");
	print_property(proxy, "Codec");
	print_property(proxy, "Capabilities");
	print_property(proxy, "Device");
	print_property(proxy, "DelayReporting");

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static const GDBusMethodTable endpoint_methods[] = {
	{ GDBUS_ASYNC_METHOD("SetConfiguration",
					GDBUS_ARGS({ "endpoint", "o" },
						{ "properties", "a{sv}" } ),
					NULL, endpoint_set_configuration) },
	{ GDBUS_ASYNC_METHOD("ClearConfiguration",
					GDBUS_ARGS({ "transport", "o" } ),
					NULL, endpoint_clear_configuration) },
	{ },
};

static void endpoint_free(void *data)
{
	struct endpoint *ep = data;

	if (ep->caps) {
		g_free(ep->caps->iov_base);
		g_free(ep->caps);
	}

	g_free(ep->path);
	g_free(ep->uuid);
	g_free(ep);
}

static gboolean endpoint_get_uuid(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct endpoint *ep = data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &ep->uuid);

	return TRUE;
}

static gboolean endpoint_get_codec(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct endpoint *ep = data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_BYTE, &ep->codec);

	return TRUE;
}

static gboolean endpoint_get_capabilities(const GDBusPropertyTable *property,
					DBusMessageIter *iter, void *data)
{
	struct endpoint *ep = data;
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
					DBUS_TYPE_BYTE_AS_STRING, &array);

	dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
					     &ep->caps->iov_base,
					     ep->caps->iov_len);

	dbus_message_iter_close_container(iter, &array);

	return TRUE;
}

static const GDBusPropertyTable endpoint_properties[] = {
	{ "UUID", "s", endpoint_get_uuid, NULL, NULL },
	{ "Codec", "y", endpoint_get_codec, NULL, NULL },
	{ "Capabilities", "ay", endpoint_get_capabilities, NULL, NULL },
	{ }
};

static uint8_t *str2bytearray(char *arg, size_t *val_len)
{
	uint8_t value[UINT8_MAX];
	char *entry;
	unsigned int i;

	for (i = 0; (entry = strsep(&arg, " \t")) != NULL; i++) {
		long val;
		char *endptr = NULL;

		if (*entry == '\0')
			continue;

		if (i >= G_N_ELEMENTS(value)) {
			bt_shell_printf("Too much data\n");
			return NULL;
		}

		val = strtol(entry, &endptr, 0);
		if (!endptr || *endptr != '\0' || val > UINT8_MAX) {
			bt_shell_printf("Invalid value at index %d\n", i);
			return NULL;
		}

		value[i] = val;
	}

	*val_len = i;

	return g_memdup(value, i);
}

static void register_endpoint_setup(DBusMessageIter *iter, void *user_data)
{
	struct endpoint *ep = user_data;
	DBusMessageIter dict;
	const char *key = "Capabilities";

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &ep->path);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	g_dbus_dict_append_entry(&dict, "UUID", DBUS_TYPE_STRING, &ep->uuid);

	g_dbus_dict_append_entry(&dict, "Codec", DBUS_TYPE_BYTE, &ep->codec);

	g_dbus_dict_append_basic_array(&dict, DBUS_TYPE_STRING, &key,
					DBUS_TYPE_BYTE, &ep->caps->iov_base,
					ep->caps->iov_len);

	bt_shell_printf("Capabilities:\n");
	bt_shell_hexdump(ep->caps->iov_base, ep->caps->iov_len);

	dbus_message_iter_close_container(iter, &dict);
}

static void register_endpoint_reply(DBusMessage *message, void *user_data)
{
	struct endpoint *ep = user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message)) {
		bt_shell_printf("Failed to register endpoint: %s\n",
				error.name);
		dbus_error_free(&error);
		local_endpoints = g_list_remove(local_endpoints, ep);
		g_dbus_unregister_interface(dbus_conn, ep->path,
						BLUEZ_MEDIA_ENDPOINT_INTERFACE);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Endpoint %s registered\n", ep->path);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void endpoint_register(struct endpoint *ep)
{
	GList *l;

	for (l = medias; l; l = g_list_next(l)) {
		if (!g_dbus_proxy_method_call(l->data, "RegisterEndpoint",
						register_endpoint_setup,
						register_endpoint_reply,
						ep, NULL)) {
			bt_shell_printf("Failed register endpoint\n");
			local_endpoints = g_list_remove(local_endpoints, ep);
			g_dbus_unregister_interface(dbus_conn, ep->path,
						BLUEZ_MEDIA_ENDPOINT_INTERFACE);
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
	}
}

static void endpoint_auto_accept(const char *input, void *user_data)
{
	struct endpoint *ep = user_data;

	if (!strcasecmp(input, "y") || !strcasecmp(input, "yes"))
		ep->auto_accept = true;
	else if (!strcasecmp(input, "n") || !strcasecmp(input, "no"))
		ep->auto_accept = false;
	else
		bt_shell_printf("Invalid input for Auto Accept\n");

	endpoint_register(ep);
}

static void endpoint_set_capabilities(const char *input, void *user_data)
{
	struct endpoint *ep = user_data;

	if (ep->caps)
		g_free(ep->caps->iov_base);
	else
		ep->caps = g_new0(struct iovec, 1);

	ep->caps->iov_base = str2bytearray((char *) input, &ep->caps->iov_len);

	bt_shell_prompt_input(ep->path, "Auto Accept (yes/no):",
						endpoint_auto_accept, ep);
}

struct codec_capabilities {
	uint8_t len;
	uint8_t type;
	uint8_t data[UINT8_MAX];
};

#define data(args...) ((const unsigned char[]) { args })

#define SBC_DATA(args...) \
	{ \
		.iov_base = (void *)data(args), \
		.iov_len = sizeof(data(args)), \
	}

#define CODEC_CAPABILITIES(_uuid, _codec_id, _data) \
	{ \
		.uuid = _uuid, \
		.codec_id = _codec_id, \
		.data = _data, \
	}

static const struct capabilities {
	const char *uuid;
	uint8_t codec_id;
	struct iovec data;
} caps[] = {
	/* A2DP SBC Source:
	 *
	 * Channel Modes: Mono DualChannel Stereo JointStereo
	 * Frequencies: 16Khz 32Khz 44.1Khz 48Khz
	 * Subbands: 4 8
	 * Blocks: 4 8 12 16
	 * Bitpool Range: 2-64
	 */
	CODEC_CAPABILITIES(A2DP_SOURCE_UUID, A2DP_CODEC_SBC,
					SBC_DATA(0xff, 0xff, 2, 64)),
	/* A2DP SBC Sink:
	 *
	 * Channel Modes: Mono DualChannel Stereo JointStereo
	 * Frequencies: 16Khz 32Khz 44.1Khz 48Khz
	 * Subbands: 4 8
	 * Blocks: 4 8 12 16
	 * Bitpool Range: 2-64
	 */
	CODEC_CAPABILITIES(A2DP_SINK_UUID, A2DP_CODEC_SBC,
					SBC_DATA(0xff, 0xff, 2, 64)),
};

static char *uuid_generator(const char *text, int state)
{
	int len = strlen(text);
	static int index = 0;
	size_t i;

	if (!state)
		index = 0;

	for (i = index; i < ARRAY_SIZE(caps); i++) {
		const struct capabilities *cap = &caps[i];

		index++;

		if (!strncasecmp(cap->uuid, text, len))
			return strdup(cap->uuid);
	}

	return NULL;
}

static const struct capabilities *find_capabilities(const char *uuid,
							uint8_t codec_id)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(caps); i++) {
		const struct capabilities *cap = &caps[i];

		if (strcasecmp(cap->uuid, uuid))
			continue;

		if (cap->codec_id == codec_id)
			return cap;
	}

	return NULL;
}

static struct iovec *iov_append(struct iovec **iov, const void *data,
							size_t len)
{
	if (!*iov) {
		*iov = new0(struct iovec, 1);
		(*iov)->iov_base = new0(uint8_t, UINT8_MAX);
	}

	if (data && len) {
		memcpy((*iov)->iov_base + (*iov)->iov_len, data, len);
		(*iov)->iov_len += len;
	}

	return *iov;
}

static void cmd_register_endpoint(int argc, char *argv[])
{
	struct endpoint *ep;
	char *endptr = NULL;

	ep = g_new0(struct endpoint, 1);
	ep->uuid = g_strdup(argv[1]);
	ep->codec = strtol(argv[2], &endptr, 0);
	ep->path = g_strdup_printf("%s/ep%u", BLUEZ_MEDIA_ENDPOINT_PATH,
					g_list_length(local_endpoints));
	local_endpoints = g_list_append(local_endpoints, ep);

	if (!g_dbus_register_interface(dbus_conn, ep->path,
					BLUEZ_MEDIA_ENDPOINT_INTERFACE,
					endpoint_methods, NULL,
					endpoint_properties, ep,
					endpoint_free)) {
		bt_shell_printf("Failed to register endpoint object\n");
		local_endpoints = g_list_remove(local_endpoints, ep);
		endpoint_free(ep);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (argc > 3)
		endpoint_set_capabilities(argv[3], ep);
	else {
		const struct capabilities *cap;

		cap = find_capabilities(ep->uuid, ep->codec);
		if (cap) {
			if (ep->caps)
				ep->caps->iov_len = 0;

			/* Copy capabilities */
			iov_append(&ep->caps, cap->data.iov_base,
							cap->data.iov_len);

			bt_shell_prompt_input(ep->path, "Auto Accept (yes/no):",
						endpoint_auto_accept, ep);
		} else
			bt_shell_prompt_input(ep->path, "Enter capabilities:",
						endpoint_set_capabilities, ep);
	}
}

static void unregister_endpoint_setup(DBusMessageIter *iter, void *user_data)
{
	struct endpoint *ep = user_data;

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH, &ep->path);
}

static void unregister_endpoint_reply(DBusMessage *message, void *user_data)
{
	struct endpoint *ep = user_data;
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message)) {
		bt_shell_printf("Failed to unregister endpoint: %s\n",
				error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Endpoint %s unregistered\n", ep->path);

	local_endpoints = g_list_remove(local_endpoints, ep);
	g_dbus_unregister_interface(dbus_conn, ep->path,
					BLUEZ_MEDIA_ENDPOINT_INTERFACE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_unregister_endpoint(int argc, char *argv[])
{
	struct endpoint *ep;
	GList *l;

	ep = endpoint_find(argv[1]);
	if (!ep) {
		bt_shell_printf("Failed to unregister endpoint object\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	for (l = medias; l; l = g_list_next(l)) {
		if (!g_dbus_proxy_method_call(l->data, "UnregisterEndpoint",
						unregister_endpoint_setup,
						unregister_endpoint_reply,
						ep, NULL)) {
			bt_shell_printf("Failed unregister endpoint\n");
			return bt_shell_noninteractive_quit(EXIT_FAILURE);
		}
	}

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

struct codec_qos {
	uint32_t interval;
	uint8_t  framing;
	char *phy;
	uint16_t sdu;
	uint8_t  rtn;
	uint16_t latency;
	uint32_t delay;
};

struct endpoint_config {
	GDBusProxy *proxy;
	struct endpoint *ep;
	struct iovec *caps;
	struct codec_qos qos;
};

static void config_endpoint_setup(DBusMessageIter *iter, void *user_data)
{
	struct endpoint_config *cfg = user_data;
	DBusMessageIter dict;
	const char *key = "Capabilities";

	dbus_message_iter_append_basic(iter, DBUS_TYPE_OBJECT_PATH,
					&cfg->ep->path);

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict);

	bt_shell_printf("Capabilities: ");
	bt_shell_hexdump(cfg->caps->iov_base, cfg->caps->iov_len);

	g_dbus_dict_append_basic_array(&dict, DBUS_TYPE_STRING, &key,
					DBUS_TYPE_BYTE, &cfg->caps->iov_base,
					cfg->caps->iov_len);

	bt_shell_printf("Interval %u\n", cfg->qos.interval);

	g_dbus_dict_append_entry(&dict, "Interval", DBUS_TYPE_UINT32,
						&cfg->qos.interval);

	bt_shell_printf("Framing %s\n", cfg->qos.framing ? "true" : "false");

	g_dbus_dict_append_entry(&dict, "Framing", DBUS_TYPE_BOOLEAN,
						&cfg->qos.framing);

	bt_shell_printf("PHY %s\n", cfg->qos.phy);

	g_dbus_dict_append_entry(&dict, "PHY", DBUS_TYPE_STRING, &cfg->qos.phy);

	bt_shell_printf("SDU %u\n", cfg->qos.sdu);

	g_dbus_dict_append_entry(&dict, "SDU", DBUS_TYPE_UINT16,
						&cfg->qos.sdu);

	bt_shell_printf("Retransmissions %u\n", cfg->qos.rtn);

	g_dbus_dict_append_entry(&dict, "Retransmissions", DBUS_TYPE_BYTE,
						&cfg->qos.rtn);

	bt_shell_printf("Latency %u\n", cfg->qos.latency);

	g_dbus_dict_append_entry(&dict, "Latency", DBUS_TYPE_UINT16,
						&cfg->qos.latency);

	bt_shell_printf("Delay %u\n", cfg->qos.delay);

	g_dbus_dict_append_entry(&dict, "Delay", DBUS_TYPE_UINT32,
						&cfg->qos.delay);

	dbus_message_iter_close_container(iter, &dict);
}

static void config_endpoint_reply(DBusMessage *message, void *user_data)
{
	struct endpoint_config *cfg = user_data;
	struct endpoint *ep = cfg->ep;
	DBusError error;

	free(cfg->caps->iov_base);
	free(cfg->caps);
	free(cfg);

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message)) {
		bt_shell_printf("Failed to config endpoint: %s\n",
				error.name);
		dbus_error_free(&error);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Endpoint %s configured\n", ep->path);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void endpoint_set_config(struct endpoint_config *cfg)
{
	if (!g_dbus_proxy_method_call(cfg->proxy, "SetConfiguration",
						config_endpoint_setup,
						config_endpoint_reply,
						cfg, NULL)) {
		bt_shell_printf("Failed to config endpoint\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}
}

static void qos_delay(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.delay = strtol(input, NULL, 0);

	endpoint_set_config(cfg);
}

static void qos_latency(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.latency = strtol(input, NULL, 0);

	bt_shell_prompt_input(cfg->ep->path, "Enter Delay:", qos_delay, cfg);
}

static void qos_rtn(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.rtn = strtol(input, NULL, 0);

	bt_shell_prompt_input(cfg->ep->path, "Enter Latency:",
							qos_latency, cfg);
}

static void qos_sdu(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.sdu = strtol(input, NULL, 0);

	bt_shell_prompt_input(cfg->ep->path, "Enter Retransmissions:",
							qos_rtn, cfg);
}

static void qos_phy(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.phy = strdup(input);

	bt_shell_prompt_input(cfg->ep->path, "Enter SDU:", qos_sdu, cfg);
}

static void qos_framing(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.framing = strtol(input, NULL, 0);

	bt_shell_prompt_input(cfg->ep->path, "Enter PHY:", qos_phy, cfg);
}

static void qos_interval(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;

	cfg->qos.interval = strtol(input, NULL, 0);

	bt_shell_prompt_input(cfg->ep->path, "Enter Framing:", qos_framing,
									cfg);
}

static void endpoint_config(const char *input, void *user_data)
{
	struct endpoint_config *cfg = user_data;
	uint8_t *data;
	size_t len;

	data = str2bytearray((char *) input, &len);

	iov_append(&cfg->caps, data, len);

	bt_shell_prompt_input(cfg->ep->path, "Enter Interval:", qos_interval,
									cfg);
}

struct codec_preset {
	const char *name;
	const struct iovec data;
};

#define SBC_PRESET(_name, _data) \
	{ \
		.name = _name, \
		.data = _data, \
	}

static const struct codec_preset sbc_presets[] = {
	/* Table 4.7: Recommended sets of SBC parameters in the SRC device
	 * Other settings: Block length = 16, Allocation method = Loudness,
	 * Subbands = 8.
	 */
	SBC_PRESET("MQ_MONO_44_1",
		SBC_DATA(0x28, 0x15, 2, SBC_BITPOOL_MQ_MONO_44100)),
	SBC_PRESET("MQ_MONO_48_1",
		SBC_DATA(0x18, 0x15, 2, SBC_BITPOOL_MQ_MONO_48000)),
	SBC_PRESET("MQ_STEREO_44_1",
		SBC_DATA(0x21, 0x15, 2, SBC_BITPOOL_MQ_JOINT_STEREO_44100)),
	SBC_PRESET("MQ_STEREO_48_1",
		SBC_DATA(0x11, 0x15, 2, SBC_BITPOOL_MQ_JOINT_STEREO_48000)),
	SBC_PRESET("HQ_MONO_44_1",
		SBC_DATA(0x28, 0x15, 2, SBC_BITPOOL_HQ_MONO_44100)),
	SBC_PRESET("HQ_MONO_48_1",
		SBC_DATA(0x18, 0x15, 2, SBC_BITPOOL_HQ_MONO_48000)),
	SBC_PRESET("HQ_STEREO_44_1",
		SBC_DATA(0x21, 0x15, 2, SBC_BITPOOL_HQ_JOINT_STEREO_44100)),
	SBC_PRESET("HQ_STEREO_48_1",
		SBC_DATA(0x11, 0x15, 2, SBC_BITPOOL_HQ_JOINT_STEREO_48000)),
};

#define PRESET(_uuid, _presets) \
	{ \
		.uuid = _uuid, \
		.presets = _presets, \
		.num_presets = ARRAY_SIZE(_presets), \
	}

static const struct preset {
	const char *uuid;
	const struct codec_preset *presets;
	size_t num_presets;
} presets[] = {
	PRESET(A2DP_SOURCE_UUID, sbc_presets),
	PRESET(A2DP_SINK_UUID, sbc_presets),
};

static const struct codec_preset *find_preset(const char *uuid,
						const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(presets); i++) {
		const struct preset *preset = &presets[i];

		if (!strcasecmp(preset->uuid, uuid)) {
			size_t j;

			for (j = 0; j < preset->num_presets; j++) {
				const struct codec_preset *p;

				p = &preset->presets[j];

				if (!strcmp(p->name, name))
					return p;
			}
		}
	}

	return NULL;
}

static void cmd_config_endpoint(int argc, char *argv[])
{
	struct endpoint_config *cfg;
	const struct codec_preset *preset;

	cfg = new0(struct endpoint_config, 1);

	cfg->proxy = g_dbus_proxy_lookup(endpoints, NULL, argv[1],
						BLUEZ_MEDIA_ENDPOINT_INTERFACE);
	if (!cfg->proxy) {
		bt_shell_printf("Endpoint %s not found\n", argv[1]);
		goto fail;
	}

	cfg->ep = endpoint_find(argv[2]);
	if (!cfg->ep) {
		bt_shell_printf("Local Endpoint %s not found\n", argv[2]);
		goto fail;
	}

	if (argc > 3) {
		preset = find_preset(cfg->ep->uuid, argv[3]);
		if (!preset) {
			bt_shell_printf("Preset %s not found\n", argv[3]);
			goto fail;
		}

		/* Copy capabilities */
		iov_append(&cfg->caps, preset->data.iov_base,
						preset->data.iov_len);

		endpoint_set_config(cfg);
		return;
	}

	bt_shell_prompt_input(cfg->ep->path, "Enter configuration:",
					endpoint_config, cfg);

	return;

fail:
	g_free(cfg);
	return bt_shell_noninteractive_quit(EXIT_FAILURE);
}

static const struct bt_shell_menu endpoint_menu = {
	.name = "endpoint",
	.desc = "Media Endpoint Submenu",
	.entries = {
	{ "list",         "[local]",    cmd_list_endpoints,
						"List available endpoints" },
	{ "show",         "<endpoint>", cmd_show_endpoint,
						"Endpoint information",
						endpoint_generator },
	{ "register",     "<UUID> <codec> [capabilities...]",
						cmd_register_endpoint,
						"Register Endpoint",
						uuid_generator },
	{ "unregister",   "<UUID/object>", cmd_unregister_endpoint,
						"Register Endpoint",
						local_endpoint_generator },
	{ "config",       "<endpoint> <local endpoint> [preset]",
						cmd_config_endpoint,
						"Configure Endpoint",
						endpoint_generator },
	{} },
};

static void media_added(GDBusProxy *proxy)
{
	medias = g_list_append(medias, proxy);

	print_media(proxy, COLORED_NEW);
}

static void player_added(GDBusProxy *proxy)
{
	players = g_list_append(players, proxy);

	if (default_player == NULL)
		default_player = proxy;

	print_player(proxy, COLORED_NEW);
}

static void print_folder(GDBusProxy *proxy, const char *description)
{
	const char *path;

	path = g_dbus_proxy_get_path(proxy);

	bt_shell_printf("%s%s%sFolder %s\n", description ? "[" : "",
					description ? : "",
					description ? "] " : "",
					path);
}

static void folder_added(GDBusProxy *proxy)
{
	folders = g_list_append(folders, proxy);

	print_folder(proxy, COLORED_NEW);
}

static void print_item(GDBusProxy *proxy, const char *description)
{
	const char *path, *name;
	DBusMessageIter iter;

	path = g_dbus_proxy_get_path(proxy);

	if (g_dbus_proxy_get_property(proxy, "Name", &iter))
		dbus_message_iter_get_basic(&iter, &name);
	else
		name = "<unknown>";

	bt_shell_printf("%s%s%sItem %s %s\n", description ? "[" : "",
					description ? : "",
					description ? "] " : "",
					path, name);
}

static void item_added(GDBusProxy *proxy)
{
	items = g_list_append(items, proxy);

	print_item(proxy, COLORED_NEW);
}

static void endpoint_added(GDBusProxy *proxy)
{
	endpoints = g_list_append(endpoints, proxy);

	print_endpoint(proxy, COLORED_NEW);
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, BLUEZ_MEDIA_INTERFACE))
		media_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_added(proxy);
	else if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_added(proxy);
}

static void media_removed(GDBusProxy *proxy)
{
	print_media(proxy, COLORED_DEL);

	medias = g_list_remove(medias, proxy);
}

static void player_removed(GDBusProxy *proxy)
{
	print_player(proxy, COLORED_DEL);

	if (default_player == proxy)
		default_player = NULL;

	players = g_list_remove(players, proxy);
}

static void folder_removed(GDBusProxy *proxy)
{
	folders = g_list_remove(folders, proxy);

	print_folder(proxy, COLORED_DEL);
}

static void item_removed(GDBusProxy *proxy)
{
	items = g_list_remove(items, proxy);

	print_item(proxy, COLORED_DEL);
}

static void endpoint_removed(GDBusProxy *proxy)
{
	endpoints = g_list_remove(endpoints, proxy);

	print_endpoint(proxy, COLORED_DEL);
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, BLUEZ_MEDIA_INTERFACE))
		media_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_removed(proxy);
	if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_removed(proxy);
}

static void player_property_changed(GDBusProxy *proxy, const char *name,
						DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Player", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

static void folder_property_changed(GDBusProxy *proxy, const char *name,
						DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Folder", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

static void item_property_changed(GDBusProxy *proxy, const char *name,
						DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Item", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

static void endpoint_property_changed(GDBusProxy *proxy, const char *name,
						DBusMessageIter *iter)
{
	char *str;

	str = proxy_description(proxy, "Endpoint", COLORED_CHG);
	print_iter(str, name, iter);
	g_free(str);
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, BLUEZ_MEDIA_PLAYER_INTERFACE))
		player_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_FOLDER_INTERFACE))
		folder_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ITEM_INTERFACE))
		item_property_changed(proxy, name, iter);
	else if (!strcmp(interface, BLUEZ_MEDIA_ENDPOINT_INTERFACE))
		endpoint_property_changed(proxy, name, iter);
}

static GDBusClient *client;

void player_add_submenu(void)
{
	bt_shell_add_submenu(&player_menu);
	bt_shell_add_submenu(&endpoint_menu);

	dbus_conn = bt_shell_get_env("DBUS_CONNECTION");
	if (!dbus_conn || client)
		return;

	client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");

	g_dbus_client_set_proxy_handlers(client, proxy_added, proxy_removed,
							property_changed, NULL);
	g_dbus_client_set_disconnect_watch(client, disconnect_handler, NULL);
}

void player_remove_submenu(void)
{
	g_dbus_client_unref(client);
}
