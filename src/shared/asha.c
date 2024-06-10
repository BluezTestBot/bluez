// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2024  Asymptotic Inc.
 *
 *  Author: Arun Raghavan <arun@asymptotic.io>
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bluetooth.h"
#include "lib/uuid.h"

#include "src/shared/util.h"
#include "src/shared/att.h"
#include "src/log.h"

#include "src/shared/queue.h"
#include "src/shared/gatt-db.h"
#include "src/shared/gatt-client.h"

#include "asha.h"

/* We use strings instead of uint128_t to maintain readability */
#define ASHA_CHRC_READ_ONLY_PROPERTIES_UUID "6333651e-c481-4a3e-9169-7c902aad37bb"
#define ASHA_CHRC_AUDIO_CONTROL_POINT_UUID "f0d4de7e-4a88-476c-9d9f-1937b0996cc0"
#define ASHA_CHRC_AUDIO_STATUS_UUID "38663f1a-e711-4cac-b641-326b56404837"
#define ASHA_CHRC_VOLUME_UUID "00e4ca9e-ab14-41e4-8823-f9e70c7e91df"
#define ASHA_CHRC_LE_PSM_OUT_UUID "2d410339-82b6-42aa-b34e-e2e01df8cc1a"

struct bt_asha *bt_asha_new(void)
{
	struct bt_asha *asha;

	asha = new0(struct bt_asha, 1);

	return asha;
}

void bt_asha_reset(struct bt_asha *asha)
{
	if (asha->status_notify_id) {
		bt_gatt_client_unregister_notify(asha->client,
						asha->status_notify_id);
	}

	gatt_db_unref(asha->db);
	asha->db = NULL;

	bt_gatt_client_unref(asha->client);
	asha->client = NULL;

	asha->psm = 0;
}

void bt_asha_state_reset(struct bt_asha *asha)
{
	asha->state = ASHA_STOPPED;

	asha->cb = NULL;
	asha->cb_user_data = NULL;
}

void bt_asha_free(struct bt_asha *asha)
{
	gatt_db_unref(asha->db);
	bt_gatt_client_unref(asha->client);
	free(asha);
}

static void asha_acp_sent(bool success, uint8_t err, void *user_data)
{
	struct bt_asha *asha = user_data;

	if (success) {
		DBG("AudioControlPoint command successfully sent");
	} else {
		error("Failed to send AudioControlPoint command: %d", err);

		if (asha->cb)
			asha->cb(-1, asha->cb_user_data);

		bt_asha_state_reset(asha);
	}
}

static int asha_send_acp(struct bt_asha *asha, uint8_t *cmd,
		unsigned int len, bt_asha_cb_t cb, void *user_data)
{
	if (!bt_gatt_client_write_value(asha->client, asha->acp_handle, cmd,
				len, asha_acp_sent, asha, NULL)) {
		error("Error writing ACP command");
		return -1;
	}

	asha->cb = cb;
	asha->cb_user_data = user_data;

	return 0;
}

unsigned int bt_asha_start(struct bt_asha *asha, bt_asha_cb_t cb,
								void *user_data)
{
	uint8_t acp_start_cmd[] = {
		0x01,		/* START */
		0x01,		/* G.722, 16 kHz */
		0,		/* Unknown media type */
		asha->volume,	/* Volume */
		0,		/* Other disconnected */
	};
	int ret;

	if (asha->state != ASHA_STOPPED) {
		error("ASHA device start failed. Bad state %d", asha->state);
		return 0;
	}

	ret = asha_send_acp(asha, acp_start_cmd, sizeof(acp_start_cmd), cb,
			user_data);
	if (ret < 0)
		return ret;

	asha->state = ASHA_STARTING;

	return 0;
}

unsigned int bt_asha_stop(struct bt_asha *asha, bt_asha_cb_t cb,
								void *user_data)
{
	uint8_t acp_stop_cmd[] = {
		0x02, /* STOP */
	};
	int ret;

	if (asha->state != ASHA_STARTED)
		return 0;

	asha->state = ASHA_STOPPING;

	return asha_send_acp(asha, acp_stop_cmd, sizeof(acp_stop_cmd),
								cb, user_data);
}

bool bt_asha_set_volume(struct bt_asha *asha, int8_t volume)
{
	if (!bt_gatt_client_write_without_response(asha->client,
						asha->volume_handle, false,
						(const uint8_t *)&volume, 1)) {
		error("Error writing volume");
		return false;
	}

	asha->volume = volume;
	return true;
}

static bool uuid_cmp(const char *uuid1, const bt_uuid_t *uuid2)
{
	bt_uuid_t lhs;

	bt_string_to_uuid(&lhs, uuid1);

	return bt_uuid_cmp(&lhs, uuid2) == 0;
}

static void read_psm(bool success,
			uint8_t att_ecode,
			const uint8_t *value,
			uint16_t length,
			void *user_data)
{
	struct bt_asha *asha = user_data;

	if (!success) {
		DBG("Reading PSM failed with ATT error: %u", att_ecode);
		return;
	}

	if (length != 2) {
		DBG("Reading PSM failed: unexpected length %u", length);
		return;
	}

	asha->psm = get_le16(value);

	DBG("Got PSM: %u", asha->psm);
}

static void read_rops(bool success,
			uint8_t att_ecode,
			const uint8_t *value,
			uint16_t length,
			void *user_data)
{
	struct bt_asha *asha = user_data;

	if (!success) {
		DBG("Reading ROPs failed with ATT error: %u", att_ecode);
		return;
	}

	if (length != 17) {
		DBG("Reading ROPs failed: unexpected length %u", length);
		return;
	}

	if (value[0] != 0x01) {
		DBG("Unexpected ASHA version: %u", value[0]);
		return;
	}

	/* Device Capabilities */
	asha->right_side = (value[1] & 0x1) != 0;
	asha->binaural = (value[1] & 0x2) != 0;
	asha->csis_supported = (value[1] & 0x4) != 0;
	/* HiSyncId: 2 byte company id, 6 byte ID shared by left and right */
	memcpy(asha->hisyncid, &value[2], 8);
	/* FeatureMap */
	asha->coc_streaming_supported = (value[10] & 0x1) != 0;
	/* RenderDelay */
	asha->render_delay = get_le16(&value[11]);
	/* byte 13 & 14 are reserved */
	/* Codec IDs */
	asha->codec_ids = get_le16(&value[15]);

	DBG("Got ROPS: side %u, binaural %u, csis: %u, delay %u, codecs: %u",
			asha->right_side, asha->binaural, asha->csis_supported,
			asha->render_delay, asha->codec_ids);
}

static void audio_status_register(uint16_t att_ecode, void *user_data)
{
	if (att_ecode)
		DBG("AudioStatusPoint register failed 0x%04x", att_ecode);
	else
		DBG("AudioStatusPoint register succeeded");
}

static void audio_status_notify(uint16_t value_handle, const uint8_t *value,
					uint16_t length, void *user_data)
{
	struct bt_asha *asha = user_data;
	uint8_t status = *value;
	/* Back these up to survive the reset paths */
	bt_asha_cb_t cb = asha->cb;
	bt_asha_cb_t cb_user_data = asha->cb_user_data;

	if (asha->state == ASHA_STARTING) {
		if (status == 0) {
			asha->state = ASHA_STARTED;
			DBG("ASHA start complete");
		} else {
			bt_asha_state_reset(asha);
			DBG("ASHA start failed");
		}
	} else if (asha->state == ASHA_STOPPING) {
		/* We reset our state, regardless */
		bt_asha_state_reset(asha);
		DBG("ASHA stop %s", status == 0 ? "complete" : "failed");
	}

	if (cb) {
		cb(status, cb_user_data);
		asha->cb = NULL;
		asha->cb_user_data = NULL;
	}
}

static void handle_characteristic(struct gatt_db_attribute *attr,
								void *user_data)
{
	struct bt_asha *asha = user_data;
	uint16_t value_handle;
	bt_uuid_t uuid;
	char uuid_str[MAX_LEN_UUID_STR];

	if (!gatt_db_attribute_get_char_data(attr, NULL, &value_handle, NULL,
								NULL, &uuid)) {
		error("Failed to obtain characteristic data");
		return;
	}

	bt_uuid_to_string(&uuid, uuid_str, sizeof(uuid_str));
	if (uuid_cmp(ASHA_CHRC_LE_PSM_OUT_UUID, &uuid)) {
		DBG("Got chrc %s/0x%x: LE_PSM_ID", uuid_str, value_handle);
		if (!bt_gatt_client_read_value(asha->client, value_handle,
					read_psm, asha, NULL))
			DBG("Failed to send request to read battery level");
	} else if (uuid_cmp(ASHA_CHRC_READ_ONLY_PROPERTIES_UUID, &uuid)) {
		DBG("Got chrc %s/0x%x: READ_ONLY_PROPERTIES", uuid_str,
								value_handle);
		if (!bt_gatt_client_read_value(asha->client, value_handle,
					read_rops, asha, NULL))
			DBG("Failed to send request for readonly properties");
	} else if (uuid_cmp(ASHA_CHRC_AUDIO_CONTROL_POINT_UUID, &uuid)) {
		DBG("Got chrc %s/0x%x: AUDIO_CONTROL_POINT", uuid_str,
								value_handle);
		/* Store this for later writes */
		asha->acp_handle = value_handle;
	} else if (uuid_cmp(ASHA_CHRC_VOLUME_UUID, &uuid)) {
		DBG("Got chrc %s/0x%x: VOLUME", uuid_str, value_handle);
		/* Store this for later writes */
		asha->volume_handle = value_handle;
	} else if (uuid_cmp(ASHA_CHRC_AUDIO_STATUS_UUID, &uuid)) {
		DBG("Got chrc %s/0x%x: AUDIO_STATUS", uuid_str, value_handle);
		asha->status_notify_id =
			bt_gatt_client_register_notify(asha->client,
				value_handle, audio_status_register,
				audio_status_notify, asha, NULL);
		if (!asha->status_notify_id)
			DBG("Failed to send request to notify AudioStatus");
	} else {
		DBG("Unsupported characteristic: %s", uuid_str);
	}
}

static void foreach_asha_service(struct gatt_db_attribute *attr,
							void *user_data)
{
	struct bt_asha *asha = user_data;

	DBG("Found ASHA GATT service");

	asha->attr = attr;
	gatt_db_service_set_claimed(attr, true);
	gatt_db_service_foreach_char(asha->attr, handle_characteristic, asha);
}

bool bt_asha_probe(struct bt_asha *asha, struct gatt_db *db,
						struct bt_gatt_client *client)
{
	bt_uuid_t asha_uuid;

	asha->db = gatt_db_ref(db);
	asha->client = bt_gatt_client_clone(client);

	bt_uuid16_create(&asha_uuid, ASHA_SERVICE);
	gatt_db_foreach_service(db, &asha_uuid, foreach_asha_service, asha);

	if (!asha->attr) {
		error("ASHA attribute not found");
		bt_asha_reset(asha);
		return false;
	}

	return true;
}
