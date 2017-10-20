/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <string.h>
#include <math.h>

#include "dfu-target-uc3.h"
#include "dfu-target-private.h"
#include "dfu-sector.h"

#include "fwupd-error.h"

/* Atmel AVR32 UC3 version of DFU:
 * http://www.atmel.com/images/doc32131.pdf */
#define DFU_UC3_GROUP_SELECT			0x06	/** SELECT */
#define DFU_UC3_CMD_SELECT_MEMORY		0x03
#define DFU_UC3_MEMORY_UNIT			0x00
#define DFU_UC3_MEMORY_PAGE			0x01
#define DFU_UC3_MEMORY_UNIT_FLASH		0x00
#define DFU_UC3_MEMORY_UNIT_EEPROM		0x01
#define DFU_UC3_MEMORY_UNIT_SECURITY		0x02
#define DFU_UC3_MEMORY_UNIT_CONFIGURATION	0x03
#define DFU_UC3_MEMORY_UNIT_BOOTLOADER		0x04
#define DFU_UC3_MEMORY_UNIT_SIGNATURE		0x05
#define DFU_UC3_MEMORY_UNIT_USER		0x06
#define DFU_UC3_GROUP_DOWNLOAD			0x01	/** DOWNLOAD */
#define DFU_UC3_CMD_PROGRAM_START		0x01
#define DFU_UC3_GROUP_UPLOAD			0x03	/** UPLOAD */
#define DFU_UC3_CMD_READ_MEMORY			0x00
#define DFU_UC3_CMD_BLANK_CHECK			0x01
#define DFU_UC3_GROUP_EXEC			0x04	/** EXEC */
#define DFU_UC3_CMD_ERASE			0x00
#define DFU_UC3_ERASE_EVERYTHING		0xff
#define DFU_UC3_CMD_START_APPLI			0x03
#define DFU_UC3_START_APPLI_RESET		0x00
#define DFU_UC3_START_APPLI_NO_RESET		0x01

gboolean
dfu_target_uc3_mass_erase (DfuTarget *target,
			   GCancellable *cancellable,
			   GError **error)
{
	GBytes *data_in;
	guint8 buf[3];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_EXEC;
	buf[0] = DFU_UC3_CMD_ERASE;
	buf[0] = 0xff;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot mass-erase: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
dfu_target_uc3_attach (DfuTarget *target, GCancellable *cancellable, GError **error)
{
	GBytes *data_in;
	guint8 buf[3];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_EXEC;
	buf[0] = DFU_UC3_CMD_START_APPLI;
	buf[0] = 0x00;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot attach: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_uc3_select_memory_unit:
 * @target: a #DfuTarget
 * @memory_unit: a unit, e.g. %DFU_UC3_MEMORY_UNIT_FLASH
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Selects the memory unit for the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_uc3_select_memory_unit (DfuTarget *target,
				   guint8 memory_unit,
				   GCancellable *cancellable,
				   GError **error)
{
	GBytes *data_in;
	guint8 buf[4];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_SELECT;
	buf[1] = DFU_UC3_CMD_SELECT_MEMORY;
	buf[2] = DFU_UC3_MEMORY_UNIT;
	buf[3] = memory_unit;
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot select memory unit: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_uc3_select_memory_page:
 * @target: a #DfuTarget
 * @memory_page: an address
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Selects the memory page for the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_uc3_select_memory_page (DfuTarget *target,
				   guint16 memory_page,
				   GCancellable *cancellable,
				   GError **error)
{
	GBytes *data_in;
	guint16 memory_page_le = GINT16_TO_LE (memory_page);
	guint8 buf[5];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_SELECT;
	buf[1] = DFU_UC3_CMD_SELECT_MEMORY;
	buf[2] = DFU_UC3_MEMORY_PAGE;
	memcpy (&buf[3], &memory_page_le, 2);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot select memory page: ");
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_uc3_program_start
 * @target: a #DfuTarget
 * @addr_start: an address
 * @addr_end: an address
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Selects the memory page for the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_uc3_program_start (DfuTarget *target,
			      guint16 addr_start,
			      guint16 addr_end,
			      GCancellable *cancellable,
			      GError **error)
{
	GBytes *data_in;
	guint16 addr_start_le = GINT16_TO_LE (addr_start);
	guint16 addr_end_le = GINT16_TO_LE (addr_end);
	guint8 buf[6];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_DOWNLOAD;
	buf[1] = DFU_UC3_CMD_PROGRAM_START;
	memcpy (&buf[2], &addr_start_le, 2);
	memcpy (&buf[4], &addr_end_le, 2);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot program start 0x%04x -> 0x%04x: ",
				(guint) addr_start, (guint) addr_end);
		return FALSE;
	}
	return TRUE;
}

/**
 * dfu_target_uc3_read_memory
 * @target: a #DfuTarget
 * @addr_start: an address
 * @addr_end: an address
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Reads flash data from the device.
 *
 * Return value: %TRUE for success
 **/
static gboolean
dfu_target_uc3_read_memory (DfuTarget *target,
			    guint16 addr_start,
			    guint16 addr_end,
			    GCancellable *cancellable,
			    GError **error)
{
	GBytes *data_in;
	guint16 addr_start_le = GINT16_TO_LE (addr_start);
	guint16 addr_end_le = GINT16_TO_LE (addr_end);
	guint8 buf[6];

	/* format buffer */
	buf[0] = DFU_UC3_GROUP_UPLOAD;
	buf[1] = DFU_UC3_CMD_READ_MEMORY;
	memcpy (&buf[2], &addr_start_le, 2);
	memcpy (&buf[4], &addr_end_le, 2);
	data_in = g_bytes_new_static (buf, sizeof(buf));
	if (!dfu_target_download_chunk (target, 0, data_in, cancellable, error)) {
		g_prefix_error (error, "cannot read memory 0x%04x -> 0x%04x: ",
				(guint) addr_start, (guint) addr_end);
		return FALSE;
	}
	return TRUE;
}
