/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2006-2010  Nokia Corporation
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 *  Copyright (C) 2007-2008  Fabien Chevalier <fabchevalier@free.fr>
 *
 *
 */

#include <dbus/dbus.h>
#include <stdint.h>

#define ERROR_INTERFACE "org.bluez.Error"

/* BR/EDR connection failure reasons */
#define ERR_BREDR_CONN_ALREADY_CONNECTED	"BR/EDR connection already "\
						"connected"
#define ERR_BREDR_CONN_PAGE_TIMEOUT		"BR/EDR connection page timeout"
#define ERR_BREDR_CONN_PROFILE_UNAVAILABLE	"BR/EDR connection profile "\
						"unavailable"
#define ERR_BREDR_CONN_SDP_SEARCH		"BR/EDR connection SDP search"
#define ERR_BREDR_CONN_CREATE_SOCKET		"BR/EDR connection create "\
						"socket"
#define ERR_BREDR_CONN_INVALID_ARGUMENTS	"BR/EDR connection invalid "\
						"argument"
#define ERR_BREDR_CONN_ADAPTER_NOT_POWERED	"BR/EDR connection adapter "\
						"not powered"
#define ERR_BREDR_CONN_NOT_SUPPORTED		"BR/EDR connection not "\
						"suuported"
#define ERR_BREDR_CONN_BAD_SOCKET		"BR/EDR connection bad socket"
#define ERR_BREDR_CONN_MEMORY_ALLOC		"BR/EDR connection memory "\
						"allocation"
#define ERR_BREDR_CONN_BUSY			"BR/EDR connection busy"
#define ERR_BREDR_CONN_CNCR_CONNECT_LIMIT	"BR/EDR connection concurrent "\
						"connection limit"
#define ERR_BREDR_CONN_TIMEOUT			"BR/EDR connection timeout"
#define ERR_BREDR_CONN_REFUSED			"BR/EDR connection refused"
#define ERR_BREDR_CONN_ABORT_BY_REMOTE		"BR/EDR connection aborted by "\
						"remote"
#define ERR_BREDR_CONN_ABORT_BY_LOCAL		"BR/EDR connection aborted by "\
						"local"
#define ERR_BREDR_CONN_LMP_PROTO_ERROR		"BR/EDR connection LMP "\
						"protocol error"
#define ERR_BREDR_CONN_CANCELED			"BR/EDR connection canceled"
#define ERR_BREDR_CONN_UNKNOWN			"BR/EDR connection unknown"

/* LE connection failure reasons */
#define ERR_LE_CONN_INVALID_ARGUMENTS	"LE connection invalid arguments"
#define ERR_LE_CONN_ADAPTER_NOT_POWERED	"LE connection adapter not powered"
#define ERR_LE_CONN_NOT_SUPPORTED	"LE connection not supported"
#define ERR_LE_CONN_ALREADY_CONNECTED	"LE connection already connected"
#define ERR_LE_CONN_BAD_SOCKET		"LE connection bad socket"
#define ERR_LE_CONN_MEMORY_ALLOC	"LE connection memory allocation"
#define ERR_LE_CONN_BUSY		"LE connection busy"
#define ERR_LE_CONN_REFUSED		"LE connection refused"
#define ERR_LE_CONN_CREATE_SOCKET	"LE connection create socket"
#define ERR_LE_CONN_TIMEOUT		"LE connection timeout"
#define ERR_LE_CONN_SYNC_CONNECT_LIMIT	"LE connection concurrent connection "\
					"limit"
#define ERR_LE_CONN_ABORT_BY_REMOTE	"LE connection abort by remote"
#define ERR_LE_CONN_ABORT_BY_LOCAL	"LE connection abort by local"
#define ERR_LE_CONN_LL_PROTO_ERROR	"LE connection link layer protocol "\
					"error"
#define ERR_LE_CONN_GATT_BROWSE		"LE connection GATT browsing"
#define ERR_LE_CONN_UNKNOWN		"LE connection unknown"

DBusMessage *btd_error_invalid_args(DBusMessage *msg);
DBusMessage *btd_error_invalid_args_str(DBusMessage *msg, const char *str);
DBusMessage *btd_error_busy(DBusMessage *msg);
DBusMessage *btd_error_already_exists(DBusMessage *msg);
DBusMessage *btd_error_not_supported(DBusMessage *msg);
DBusMessage *btd_error_not_connected(DBusMessage *msg);
DBusMessage *btd_error_already_connected(DBusMessage *msg);
DBusMessage *btd_error_not_available(DBusMessage *msg);
DBusMessage *btd_error_not_available_str(DBusMessage *msg, const char *str);
DBusMessage *btd_error_in_progress(DBusMessage *msg);
DBusMessage *btd_error_in_progress_str(DBusMessage *msg, const char *str);
DBusMessage *btd_error_does_not_exist(DBusMessage *msg);
DBusMessage *btd_error_not_authorized(DBusMessage *msg);
DBusMessage *btd_error_not_permitted(DBusMessage *msg, const char *str);
DBusMessage *btd_error_no_such_adapter(DBusMessage *msg);
DBusMessage *btd_error_agent_not_available(DBusMessage *msg);
DBusMessage *btd_error_not_ready(DBusMessage *msg);
DBusMessage *btd_error_not_ready_str(DBusMessage *msg, const char *str);
DBusMessage *btd_error_failed(DBusMessage *msg, const char *str);

const char* btd_error_bredr_conn_from_errno(int errno_code);
const char* btd_error_le_conn_from_errno(int errno_code);
