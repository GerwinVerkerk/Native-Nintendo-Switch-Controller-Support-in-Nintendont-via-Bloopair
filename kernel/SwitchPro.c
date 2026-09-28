/*
 * Copyright (C) 2026 GerwinVerkerk/Nintendont contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2.
 */
#include "SwitchPro.h"
#ifdef SWITCH_PRO_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

static void clear_bytes(u8 *data, u16 size)
{
	while(size--)
		*data++ = 0;
}

static void copy_bytes(u8 *target, const u8 *source, u16 size)
{
	while(size--)
		*target++ = *source++;
}

static s16 clamp_axis(s32 value)
{
	if(value > 127)
		return 127;
	if(value < -128)
		return -128;
	return (s16)value;
}

static u16 read_le16(const u8 *data)
{
	return data[0] | ((u16)data[1] << 8);
}

static u32 read_le32(const u8 *data)
{
	return data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) |
		((u32)data[3] << 24);
}

static u16 switch_axis_x(const u8 *data)
{
	return data[0] | ((data[1] & 0x0F) << 8);
}

static u16 switch_axis_y(const u8 *data)
{
	return (data[1] >> 4) | (data[2] << 4);
}

static s16 calibrate_axis(u16 raw, u16 center, u16 minimum, u16 maximum)
{
	s32 value;
	s32 range;
	if(raw >= center)
	{
		range = maximum > center ? maximum - center : 1;
		value = ((s32)raw - center) * 127 / range;
	}
	else
	{
		range = center > minimum ? center - minimum : 1;
		value = -((s32)center - raw) * 128 / range;
	}
	return clamp_axis(value);
}

static void parse_full(const struct SwitchProState *state, const u8 *report,
	struct SwitchProInput *input)
{
	u8 right = report[3];
	u8 shared = report[4];
	u8 left = report[5];
	u32 buttons = 0;

	if(state != 0 && state->left_calibrated)
	{
		input->left_x = calibrate_axis(switch_axis_x(&report[6]),
			state->left_center_x, state->left_min_x, state->left_max_x);
		input->left_y = -calibrate_axis(switch_axis_y(&report[6]),
			state->left_center_y, state->left_min_y, state->left_max_y);
	}
	else
	{
		input->left_x = clamp_axis(((s32)switch_axis_x(&report[6]) - 0x800) >> 4);
		input->left_y = clamp_axis(-(((s32)switch_axis_y(&report[6]) - 0x800) >> 4));
	}
	if(state != 0 && state->right_calibrated)
	{
		input->right_x = calibrate_axis(switch_axis_x(&report[9]),
			state->right_center_x, state->right_min_x, state->right_max_x);
		input->right_y = -calibrate_axis(switch_axis_y(&report[9]),
			state->right_center_y, state->right_min_y, state->right_max_y);
	}
	else
	{
		input->right_x = clamp_axis(((s32)switch_axis_x(&report[9]) - 0x800) >> 4);
		input->right_y = clamp_axis(-(((s32)switch_axis_y(&report[9]) - 0x800) >> 4));
	}

	if(right & 0x08) buttons |= SWITCH_PRO_BTN_A;
	if(right & 0x04) buttons |= SWITCH_PRO_BTN_B;
	if(right & 0x02) buttons |= SWITCH_PRO_BTN_X;
	if(right & 0x01) buttons |= SWITCH_PRO_BTN_Y;
	if(right & 0x40) buttons |= SWITCH_PRO_BTN_R;
	if(right & 0x80) buttons |= SWITCH_PRO_BTN_ZR;
	if(shared & 0x02) buttons |= SWITCH_PRO_BTN_PLUS;
	if(shared & 0x01) buttons |= SWITCH_PRO_BTN_MINUS;
	if(shared & 0x10) buttons |= SWITCH_PRO_BTN_HOME;
	if(left & 0x40) buttons |= SWITCH_PRO_BTN_L;
	if(left & 0x80) buttons |= SWITCH_PRO_BTN_ZL;
	if(left & 0x08) buttons |= SWITCH_PRO_BTN_LEFT;
	if(left & 0x04) buttons |= SWITCH_PRO_BTN_RIGHT;
	if(left & 0x02) buttons |= SWITCH_PRO_BTN_UP;
	if(left & 0x01) buttons |= SWITCH_PRO_BTN_DOWN;
	input->buttons = buttons;
}

static void add_dpad(u8 dpad, u32 *buttons)
{
	switch(dpad)
	{
		case 0: *buttons |= SWITCH_PRO_BTN_UP; break;
		case 1: *buttons |= SWITCH_PRO_BTN_UP | SWITCH_PRO_BTN_RIGHT; break;
		case 2: *buttons |= SWITCH_PRO_BTN_RIGHT; break;
		case 3: *buttons |= SWITCH_PRO_BTN_DOWN | SWITCH_PRO_BTN_RIGHT; break;
		case 4: *buttons |= SWITCH_PRO_BTN_DOWN; break;
		case 5: *buttons |= SWITCH_PRO_BTN_DOWN | SWITCH_PRO_BTN_LEFT; break;
		case 6: *buttons |= SWITCH_PRO_BTN_LEFT; break;
		case 7: *buttons |= SWITCH_PRO_BTN_UP | SWITCH_PRO_BTN_LEFT; break;
		default: break;
	}
}

static void parse_basic(const u8 *report, struct SwitchProInput *input)
{
	u8 primary = report[1];
	u8 shared = report[2];
	u32 buttons = 0;

	input->left_x = clamp_axis(((s32)read_le16(&report[4]) - 0x8000) >> 8);
	input->left_y = clamp_axis(-(((s32)read_le16(&report[6]) - 0x8000) >> 8));
	input->right_x = clamp_axis(((s32)read_le16(&report[8]) - 0x8000) >> 8);
	input->right_y = clamp_axis(-(((s32)read_le16(&report[10]) - 0x8000) >> 8));

	/* Simple report 0x3f follows the standard HID mapping used by SDL
	 * and BlueRetro, not the native 0x30 button layout. */
	if(primary & 0x02) buttons |= SWITCH_PRO_BTN_A;
	if(primary & 0x01) buttons |= SWITCH_PRO_BTN_B;
	if(primary & 0x08) buttons |= SWITCH_PRO_BTN_X;
	if(primary & 0x04) buttons |= SWITCH_PRO_BTN_Y;
	if(primary & 0x10) buttons |= SWITCH_PRO_BTN_L;
	if(primary & 0x20) buttons |= SWITCH_PRO_BTN_R;
	if(primary & 0x40) buttons |= SWITCH_PRO_BTN_ZL;
	if(primary & 0x80) buttons |= SWITCH_PRO_BTN_ZR;
	if(shared & 0x02) buttons |= SWITCH_PRO_BTN_PLUS;
	if(shared & 0x01) buttons |= SWITCH_PRO_BTN_MINUS;
	if(shared & 0x10) buttons |= SWITCH_PRO_BTN_HOME;
	add_dpad(report[3] & 0x0F, &buttons);
	input->buttons = buttons;
}

void SwitchProReset(struct SwitchProState *state)
{
	clear_bytes((u8*)state, sizeof(*state));
	state->drop_first_basic_report = 1;
	state->left_center_x = state->left_center_y = 0x800;
	state->right_center_x = state->right_center_y = 0x800;
	state->left_min_x = state->left_min_y = 0x266;
	state->right_min_x = state->right_min_y = 0x266;
	state->left_max_x = state->left_max_y = 0xD9A;
	state->right_max_x = state->right_max_y = 0xD9A;
}

s32 SwitchProParseReport(struct SwitchProState *state, const u8 *report,
	u16 len, struct SwitchProInput *input)
{
	if(report == 0 || input == 0 || len == 0)
		return 0;

	if((report[0] == SWITCH_PRO_REPORT_FULL ||
		report[0] == SWITCH_PRO_REPORT_COMMAND) && len >= 13)
	{
		parse_full(state, report, input);
		return 1;
	}

	if(report[0] == SWITCH_PRO_REPORT_BASIC && len >= 12)
	{
		if(state != 0 && state->drop_first_basic_report)
		{
			state->drop_first_basic_report = 0;
			return 0;
		}
		parse_basic(report, input);
		return 1;
	}

	return 0;
}

u16 SwitchProBuildSubcommand(struct SwitchProState *state, u8 *report,
	u16 capacity, u8 command, const u8 *data, u8 data_len)
{
	u16 size = 11 + data_len;
	if(report == 0 || state == 0 || capacity < size)
		return 0;

	clear_bytes(report, size);
	report[0] = 0x01;
	report[1] = state->report_counter++ & 0x0F;
	/* Match the working Linux hid-nintendo Bluetooth initialization exactly:
	 * both four-byte rumble frames are zero until vibration is enabled. */
	report[10] = command;
	if(data_len && data != 0)
		copy_bytes(&report[11], data, data_len);
	return size;
}

void SwitchProInitStart(struct SwitchProState *state)
{
	if(state == 0)
		return;
	state->init_state = SWITCH_PRO_INIT_INITIAL_DELAY;
	state->init_retries = 0;
	state->pending_subcommand = 0;
}

u16 SwitchProInitDelayMs(const struct SwitchProState *state)
{
	if(state == 0)
		return 0;
	switch(state->init_state)
	{
		case SWITCH_PRO_INIT_INITIAL_DELAY:
			/* The reference controller sends/accepts the first subcommand about
			 * 10-15 ms after both incoming HID channels are configured. */
			return 15;
		case SWITCH_PRO_INIT_DEVICE_INFO_ACKED:
		case SWITCH_PRO_INIT_PLAYER_LED_ACKED:
		case SWITCH_PRO_INIT_VIBRATION_ACKED:
		case SWITCH_PRO_INIT_USER_CAL_ACKED:
		case SWITCH_PRO_INIT_FACTORY_CAL_ACKED:
			return 10;
		case SWITCH_PRO_INIT_WAIT_DEVICE_INFO:
		case SWITCH_PRO_INIT_WAIT_PLAYER_LED:
		case SWITCH_PRO_INIT_WAIT_VIBRATION:
		case SWITCH_PRO_INIT_WAIT_USER_CAL:
		case SWITCH_PRO_INIT_WAIT_FACTORY_CAL:
		case SWITCH_PRO_INIT_WAIT_REPORT_MODE:
			return 100;
		default:
			return 0;
	}
}

u8 SwitchProInitPoll(struct SwitchProState *state)
{
	u8 command;
	if(state == 0)
		return SWITCH_PRO_INIT_ACTION_NONE;

	switch(state->init_state)
	{
		case SWITCH_PRO_INIT_INITIAL_DELAY:
			command = SWITCH_PRO_SUBCMD_DEVICE_INFO;
			state->init_state = SWITCH_PRO_INIT_WAIT_DEVICE_INFO;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_DEVICE_INFO_ACKED:
			command = SWITCH_PRO_SUBCMD_PLAYER_LED;
			state->init_state = SWITCH_PRO_INIT_WAIT_PLAYER_LED;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_PLAYER_LED_ACKED:
			command = SWITCH_PRO_SUBCMD_VIBRATION;
			state->init_state = SWITCH_PRO_INIT_WAIT_VIBRATION;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_VIBRATION_ACKED:
			command = SWITCH_PRO_SUBCMD_SPI_READ;
			state->init_state = SWITCH_PRO_INIT_WAIT_USER_CAL;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_USER_CAL_ACKED:
			command = SWITCH_PRO_SUBCMD_SPI_READ;
			state->init_state = SWITCH_PRO_INIT_WAIT_FACTORY_CAL;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_FACTORY_CAL_ACKED:
			command = SWITCH_PRO_SUBCMD_REPORT_MODE;
			state->init_state = SWITCH_PRO_INIT_WAIT_REPORT_MODE;
			state->init_retries = 1;
			break;
		case SWITCH_PRO_INIT_WAIT_DEVICE_INFO:
			command = SWITCH_PRO_SUBCMD_DEVICE_INFO;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
			{
				state->init_state = SWITCH_PRO_INIT_FAILED;
				state->pending_subcommand = 0;
				return SWITCH_PRO_INIT_ACTION_FAILED;
			}
			state->init_retries++;
			break;
		case SWITCH_PRO_INIT_WAIT_PLAYER_LED:
			command = SWITCH_PRO_SUBCMD_PLAYER_LED;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
			{
				state->init_state = SWITCH_PRO_INIT_FAILED;
				state->pending_subcommand = 0;
				return SWITCH_PRO_INIT_ACTION_FAILED;
			}
			state->init_retries++;
			break;
		case SWITCH_PRO_INIT_WAIT_VIBRATION:
			command = SWITCH_PRO_SUBCMD_VIBRATION;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
				goto init_failed;
			state->init_retries++;
			break;
		case SWITCH_PRO_INIT_WAIT_USER_CAL:
			command = SWITCH_PRO_SUBCMD_SPI_READ;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
			{
				/* Factory calibration remains usable when no user block exists. */
				state->init_state = SWITCH_PRO_INIT_USER_CAL_ACKED;
				state->init_retries = 0;
				state->pending_subcommand = 0;
				return SWITCH_PRO_INIT_ACTION_NONE;
			}
			state->init_retries++;
			break;
		case SWITCH_PRO_INIT_WAIT_FACTORY_CAL:
			command = SWITCH_PRO_SUBCMD_SPI_READ;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
			{
				/* Safe default calibration is already installed by Reset. */
				state->init_state = SWITCH_PRO_INIT_FACTORY_CAL_ACKED;
				state->init_retries = 0;
				state->pending_subcommand = 0;
				return SWITCH_PRO_INIT_ACTION_NONE;
			}
			state->init_retries++;
			break;
		case SWITCH_PRO_INIT_WAIT_REPORT_MODE:
			command = SWITCH_PRO_SUBCMD_REPORT_MODE;
			if(state->init_retries >= SWITCH_PRO_INIT_RETRY_MAX)
				goto init_failed;
			state->init_retries++;
			break;
		default:
			return SWITCH_PRO_INIT_ACTION_NONE;
	}

	state->pending_subcommand = command;
	if(command == SWITCH_PRO_SUBCMD_DEVICE_INFO)
		return SWITCH_PRO_INIT_ACTION_DEVICE_INFO;
	if(command == SWITCH_PRO_SUBCMD_PLAYER_LED)
		return SWITCH_PRO_INIT_ACTION_PLAYER_LED;
	if(command == SWITCH_PRO_SUBCMD_VIBRATION)
		return SWITCH_PRO_INIT_ACTION_VIBRATION;
	if(command == SWITCH_PRO_SUBCMD_SPI_READ)
		return state->init_state == SWITCH_PRO_INIT_WAIT_USER_CAL ?
			SWITCH_PRO_INIT_ACTION_USER_CAL : SWITCH_PRO_INIT_ACTION_FACTORY_CAL;
	return SWITCH_PRO_INIT_ACTION_REPORT_MODE;

init_failed:
	state->init_state = SWITCH_PRO_INIT_FAILED;
	state->pending_subcommand = 0;
	return SWITCH_PRO_INIT_ACTION_FAILED;
}

static void finalize_axis(u16 center, u16 below, u16 above, u16 *minimum,
	u16 *maximum)
{
	if(center == 0xFFF)
		center = 0x800;
	if(below == 0xFFF)
		below = 1434;
	if(above == 0xFFF)
		above = 1434;
	*minimum = center > below ? center - below : 0;
	*maximum = center + above < 0x1000 ? center + above : 0xFFF;
}

static void parse_left_calibration(struct SwitchProState *state, const u8 *raw)
{
	u16 max_x = switch_axis_x(&raw[0]);
	u16 max_y = switch_axis_y(&raw[0]);
	u16 center_x = switch_axis_x(&raw[3]);
	u16 center_y = switch_axis_y(&raw[3]);
	u16 min_x = switch_axis_x(&raw[6]);
	u16 min_y = switch_axis_y(&raw[6]);
	if(center_x == 0xFFF) center_x = 0x800;
	if(center_y == 0xFFF) center_y = 0x800;
	state->left_center_x = center_x;
	state->left_center_y = center_y;
	finalize_axis(center_x, min_x, max_x, &state->left_min_x,
		&state->left_max_x);
	finalize_axis(center_y, min_y, max_y, &state->left_min_y,
		&state->left_max_y);
	state->left_calibrated = 1;
}

static void parse_right_calibration(struct SwitchProState *state, const u8 *raw)
{
	u16 center_x = switch_axis_x(&raw[0]);
	u16 center_y = switch_axis_y(&raw[0]);
	u16 min_x = switch_axis_x(&raw[3]);
	u16 min_y = switch_axis_y(&raw[3]);
	u16 max_x = switch_axis_x(&raw[6]);
	u16 max_y = switch_axis_y(&raw[6]);
	if(center_x == 0xFFF) center_x = 0x800;
	if(center_y == 0xFFF) center_y = 0x800;
	state->right_center_x = center_x;
	state->right_center_y = center_y;
	finalize_axis(center_x, min_x, max_x, &state->right_min_x,
		&state->right_max_x);
	finalize_axis(center_y, min_y, max_y, &state->right_min_y,
		&state->right_max_y);
	state->right_calibrated = 1;
}

static u8 parse_spi_response(struct SwitchProState *state, const u8 *data,
	u16 data_len)
{
	u32 address;
	u8 size;
	const u8 *raw;
	if(data == 0 || data_len < 5)
		return 0;
	address = read_le32(data);
	size = data[4];
	if(data_len < (u16)(5 + size))
		return 0;
	raw = &data[5];
	if(address == SWITCH_PRO_USER_CAL_ADDR && size >= 22)
	{
		if(raw[0] == 0xB2 && raw[1] == 0xA1)
			parse_left_calibration(state, &raw[2]);
		if(raw[11] == 0xB2 && raw[12] == 0xA1)
			parse_right_calibration(state, &raw[13]);
		return 1;
	}
	if(address == SWITCH_PRO_FACTORY_CAL_ADDR && size >= 18)
	{
		if(!state->left_calibrated)
			parse_left_calibration(state, &raw[0]);
		if(!state->right_calibrated)
			parse_right_calibration(state, &raw[9]);
		return 1;
	}
	return 0;
}

u8 SwitchProInitHandleResponse(struct SwitchProState *state, u8 ack, u8 command,
	const u8 *data, u16 data_len)
{
	if(state == 0 || command != state->pending_subcommand)
		return SWITCH_PRO_ACK_IGNORED;
	if((ack & 0x80) == 0)
		return SWITCH_PRO_ACK_NEGATIVE;

	if(command == SWITCH_PRO_SUBCMD_DEVICE_INFO &&
		state->init_state == SWITCH_PRO_INIT_WAIT_DEVICE_INFO)
	{
		if(data != 0 && data_len >= 3)
			state->device_type = data[2];
		state->init_state = SWITCH_PRO_INIT_DEVICE_INFO_ACKED;
		state->init_retries = 0;
		state->pending_subcommand = 0;
		return SWITCH_PRO_ACK_ACCEPTED;
	}
	if(command == SWITCH_PRO_SUBCMD_PLAYER_LED &&
		state->init_state == SWITCH_PRO_INIT_WAIT_PLAYER_LED)
	{
		state->init_state = SWITCH_PRO_INIT_PLAYER_LED_ACKED;
		state->init_retries = 0;
		state->pending_subcommand = 0;
		return SWITCH_PRO_ACK_ACCEPTED;
	}
	if(command == SWITCH_PRO_SUBCMD_VIBRATION &&
		state->init_state == SWITCH_PRO_INIT_WAIT_VIBRATION)
	{
		state->init_state = SWITCH_PRO_INIT_VIBRATION_ACKED;
		state->init_retries = 0;
		state->pending_subcommand = 0;
		return SWITCH_PRO_ACK_ACCEPTED;
	}
	if(command == SWITCH_PRO_SUBCMD_SPI_READ &&
		(state->init_state == SWITCH_PRO_INIT_WAIT_USER_CAL ||
		 state->init_state == SWITCH_PRO_INIT_WAIT_FACTORY_CAL))
	{
		u8 was_user = state->init_state == SWITCH_PRO_INIT_WAIT_USER_CAL;
		if(!parse_spi_response(state, data, data_len))
			return SWITCH_PRO_ACK_NEGATIVE;
		if(was_user && state->left_calibrated && state->right_calibrated)
			state->init_state = SWITCH_PRO_INIT_FACTORY_CAL_ACKED;
		else if(was_user)
			state->init_state = SWITCH_PRO_INIT_USER_CAL_ACKED;
		else
			state->init_state = SWITCH_PRO_INIT_FACTORY_CAL_ACKED;
		state->init_retries = 0;
		state->pending_subcommand = 0;
		return SWITCH_PRO_ACK_ACCEPTED;
	}
	if(command == SWITCH_PRO_SUBCMD_REPORT_MODE &&
		state->init_state == SWITCH_PRO_INIT_WAIT_REPORT_MODE)
	{
		state->init_state = SWITCH_PRO_INIT_READY;
		state->init_retries = 0;
		state->pending_subcommand = 0;
		return SWITCH_PRO_ACK_ACCEPTED;
	}
	return SWITCH_PRO_ACK_IGNORED;
}

u8 SwitchProTrackStreamReport(struct SwitchProState *state, u8 report_id,
	u8 parsed)
{
	if(state == 0)
		return 0;
	if(report_id != SWITCH_PRO_REPORT_FULL &&
		report_id != SWITCH_PRO_REPORT_BASIC)
		return state->consecutive_stream_reports >=
			SWITCH_PRO_STREAM_READY_REPORTS;
	if(state->consecutive_stream_reports >= SWITCH_PRO_STREAM_READY_REPORTS)
		return 1;
	if(!parsed)
	{
		state->consecutive_stream_reports = 0;
		return 0;
	}
	if(state->consecutive_stream_reports < SWITCH_PRO_STREAM_READY_REPORTS)
		state->consecutive_stream_reports++;
	return state->consecutive_stream_reports >= SWITCH_PRO_STREAM_READY_REPORTS;
}

u8 SwitchProDiagnosticLED(u32 phase, u8 blink_on)
{
	switch(phase)
	{
		case 1: return 0x10;
		case 2: return 0x30;
		case 3: return 0x70;
		case 4: return 0xF0;
		case 5: return blink_on ? 0x50 : 0xA0;
		case 6: return blink_on ? 0xF0 : 0x00;
		case 7: return blink_on ? 0xF0 : 0x00;
		case 8: return blink_on ? 0xF0 : 0x00;
		case 9: return blink_on ? 0x90 : 0x60;
		case 10: return blink_on ? 0x30 : 0xC0;
		case 11: return 0x90;
		case 12: return 0x60;
		default: return 0x00;
	}
}

void SwitchProTransportReset(struct SwitchProTransport *transport)
{
	memset(transport, 0, sizeof(*transport));
}

void SwitchProTransportBegin(struct SwitchProTransport *transport, u8 origin)
{
	SwitchProTransportReset(transport);
	transport->origin = origin;
	transport->state = SWITCH_PRO_TRANSPORT_WAIT_ACL;
}

u8 SwitchProTransportACLReady(struct SwitchProTransport *transport)
{
	if(transport->state != SWITCH_PRO_TRANSPORT_WAIT_ACL)
		return SWITCH_PRO_TRANSPORT_ACTION_NONE;
	transport->state = SWITCH_PRO_TRANSPORT_WAIT_SECURITY;
	return SWITCH_PRO_TRANSPORT_ACTION_NONE;
}

u8 SwitchProTransportSecurityReady(struct SwitchProTransport *transport)
{
	if(transport->state != SWITCH_PRO_TRANSPORT_WAIT_SECURITY)
		return SWITCH_PRO_TRANSPORT_ACTION_NONE;
	transport->authenticated = 1;
	transport->encrypted = 1;
	if(transport->origin == SWITCH_PRO_CONNECTION_INCOMING)
	{
		if(transport->control_open && transport->interrupt_open)
		{
			transport->state = SWITCH_PRO_TRANSPORT_READY;
			return SWITCH_PRO_TRANSPORT_ACTION_READY;
		}
		transport->state = SWITCH_PRO_TRANSPORT_WAIT_INCOMING_HID;
		return SWITCH_PRO_TRANSPORT_ACTION_ACCEPT_HID;
	}
	if(transport->origin == SWITCH_PRO_CONNECTION_OUTGOING)
	{
		transport->state = SWITCH_PRO_TRANSPORT_OPEN_OUTGOING_HID;
		return SWITCH_PRO_TRANSPORT_ACTION_OPEN_HID;
	}
	transport->state = SWITCH_PRO_TRANSPORT_FAILED;
	return SWITCH_PRO_TRANSPORT_ACTION_NONE;
}

u8 SwitchProTransportChannelReady(struct SwitchProTransport *transport,
	u8 control_channel)
{
	if(control_channel)
		transport->control_open = 1;
	else
		transport->interrupt_open = 1;
	if(!transport->control_open || !transport->interrupt_open ||
		!transport->authenticated || !transport->encrypted)
		return SWITCH_PRO_TRANSPORT_ACTION_NONE;
	if(transport->state != SWITCH_PRO_TRANSPORT_WAIT_INCOMING_HID &&
		transport->state != SWITCH_PRO_TRANSPORT_OPEN_OUTGOING_HID)
		return SWITCH_PRO_TRANSPORT_ACTION_NONE;
	transport->state = SWITCH_PRO_TRANSPORT_READY;
	return SWITCH_PRO_TRANSPORT_ACTION_READY;
}

void SwitchProTransportFail(struct SwitchProTransport *transport)
{
	transport->state = SWITCH_PRO_TRANSPORT_FAILED;
}
