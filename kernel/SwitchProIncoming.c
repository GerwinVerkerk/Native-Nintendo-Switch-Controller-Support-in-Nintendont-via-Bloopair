/* Minimal incoming-only Nintendo Switch Pro Controller support. */
#include "SwitchProIncoming.h"
#ifdef SWITCH_PRO_HOST_TEST
#include <string.h>
#else
#include "string.h"
#endif

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

void SwitchProIncomingReset(struct SwitchProIncomingState *state)
{
	memset(state, 0, sizeof(*state));
	state->drop_first_basic_report = 1;
}

void SwitchProIncomingImported(struct SwitchProIncomingState *state)
{
	state->imported = 1;
}

u8 SwitchProIncomingNeedsListener(const struct SwitchProIncomingState *state)
{
	return state != 0 && state->imported && !state->listener_registered;
}

void SwitchProIncomingListener(struct SwitchProIncomingState *state, s32 result)
{
	if(result == 0)
		state->listener_registered = 1;
}

void SwitchProIncomingACL(struct SwitchProIncomingState *state, s32 result)
{
	state->acl_connected = result == 0;
}

void SwitchProIncomingAuthentication(struct SwitchProIncomingState *state,
	s32 result)
{
	state->authenticated = result == 0;
}

void SwitchProIncomingEncryption(struct SwitchProIncomingState *state,
	s32 result, u8 enabled)
{
	state->encrypted = result == 0 && enabled != 0;
}

void SwitchProIncomingChannels(struct SwitchProIncomingState *state,
	u8 control_open, u8 interrupt_open)
{
	state->control_open = control_open != 0;
	state->interrupt_open = interrupt_open != 0;
	state->connected = SwitchProIncomingReady(state);
}

u8 SwitchProIncomingReady(const struct SwitchProIncomingState *state)
{
	return state != 0 && state->imported && state->listener_registered &&
		state->acl_connected && state->authenticated && state->encrypted &&
		state->control_open && state->interrupt_open;
}

s32 SwitchProIncomingParseBasic(struct SwitchProIncomingState *state,
	const u8 *report, u16 len)
{
	u8 primary;
	u8 shared;
	u32 buttons = 0;

	if(state == 0 || report == 0 || len < 12 ||
		report[0] != SWITCH_PRO_REPORT_BASIC)
		return 0;

	state->basic_reports++;
	if(state->drop_first_basic_report)
	{
		state->drop_first_basic_report = 0;
		return 0;
	}

	primary = report[1];
	shared = report[2];
	state->input.left_x = clamp_axis(
		((s32)read_le16(&report[4]) - 0x8000) >> 8);
	state->input.left_y = clamp_axis(
		-(((s32)read_le16(&report[6]) - 0x8000) >> 8));
	state->input.right_x = clamp_axis(
		((s32)read_le16(&report[8]) - 0x8000) >> 8);
	state->input.right_y = clamp_axis(
		-(((s32)read_le16(&report[10]) - 0x8000) >> 8));

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
	add_dpad(report[3] & 0x0f, &buttons);
	state->input.buttons = buttons;
	state->input_valid = 1;
	return 1;
}
