// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2021 Google LLC
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 */

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "lib/bluetooth.h"
#include "lib/mgmt.h"

#include "src/shared/aosp.h"
#include "src/shared/util.h"

static struct {
	aosp_debug_func_t callback;
	void *data;
} aosp_debug;

void aosp_set_debug(aosp_debug_func_t callback, void *user_data)
{
	aosp_debug.callback = callback;
	aosp_debug.data = user_data;
}

bool process_aosp_quality_report(const struct mgmt_ev_quality_report *ev)
{
	const struct aosp_bqr *edata = (struct aosp_bqr *)ev->data;
	struct aosp_bqr bqr;

	if (edata->subevent_code != 0x58) {
		util_debug(aosp_debug.callback, aosp_debug.data,
			"error: %u not AOSP Bluetooth quality report subevent",
			edata->subevent_code);
		return false;
	}

	if (ev->data_len < sizeof(struct aosp_bqr)) {
		util_debug(aosp_debug.callback, aosp_debug.data,
			"error: AOSP report size %u too small (expect >= %lu).",
			ev->data_len, sizeof(struct aosp_bqr));
		return false;
	}

	/* Ignore the Vendor Specific Parameter (VSP) field for now
	 * due to the lack of standard way of reading it.
	 */
	bqr.quality_report_id = edata->quality_report_id;
	bqr.packet_type = edata->packet_type;
	bqr.conn_handle = btohs(edata->conn_handle);
	bqr.conn_role = edata->conn_role;
	bqr.tx_power_level = edata->tx_power_level;
	bqr.rssi = edata->rssi;
	bqr.snr = edata->snr;
	bqr.unused_afh_channel_count = edata->unused_afh_channel_count;
	bqr.afh_select_unideal_channel_count =
				edata->afh_select_unideal_channel_count;
	bqr.lsto = btohs(edata->lsto);
	bqr.conn_piconet_clock = btohl(edata->conn_piconet_clock);
	bqr.retransmission_count = btohl(edata->retransmission_count);
	bqr.no_rx_count = btohl(edata->no_rx_count);
	bqr.nak_count = btohl(edata->nak_count);
	bqr.last_tx_ack_timestamp = btohl(edata->last_tx_ack_timestamp);
	bqr.flow_off_count = btohl(edata->flow_off_count);
	bqr.last_flow_on_timestamp = btohl(edata->last_flow_on_timestamp);
	bqr.buffer_overflow_bytes = btohl(edata->buffer_overflow_bytes);
	bqr.buffer_underflow_bytes = btohl(edata->buffer_underflow_bytes);

	util_debug(aosp_debug.callback, aosp_debug.data,
		"AOSP report of connection hanle %u received", bqr.conn_handle);

	return true;
}
