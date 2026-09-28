#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "SwitchPro.h"

static void pack_axis(u8 *data, u16 x, u16 y)
{
	data[0] = x & 0xFF;
	data[1] = ((x >> 8) & 0x0F) | ((y & 0x0F) << 4);
	data[2] = (y >> 4) & 0xFF;
}

static void test_full_report(void)
{
	struct SwitchProState state;
	struct SwitchProInput input;
	u8 report[13];

	memset(report, 0, sizeof(report));
	SwitchProReset(&state);
	report[0] = SWITCH_PRO_REPORT_FULL;
	report[3] = 0x08 | 0x04 | 0x02 | 0x01 | 0x40 | 0x80;
	report[4] = 0x02 | 0x01 | 0x10;
	report[5] = 0x40 | 0x80 | 0x08 | 0x04 | 0x02 | 0x01;
	pack_axis(&report[6], 0xFFF, 0x000);
	pack_axis(&report[9], 0x000, 0xFFF);

	assert(SwitchProParseReport(&state, report, sizeof(report), &input) == 1);
	assert(input.left_x == 127);
	assert(input.left_y == 127);
	assert(input.right_x == -128);
	assert(input.right_y == -127);
	assert(input.buttons & SWITCH_PRO_BTN_A);
	assert(input.buttons & SWITCH_PRO_BTN_B);
	assert(input.buttons & SWITCH_PRO_BTN_X);
	assert(input.buttons & SWITCH_PRO_BTN_Y);
	assert(input.buttons & SWITCH_PRO_BTN_R);
	assert(input.buttons & SWITCH_PRO_BTN_ZR);
	assert(input.buttons & SWITCH_PRO_BTN_PLUS);
	assert(input.buttons & SWITCH_PRO_BTN_MINUS);
	assert(input.buttons & SWITCH_PRO_BTN_HOME);
	assert(input.buttons & SWITCH_PRO_BTN_L);
	assert(input.buttons & SWITCH_PRO_BTN_ZL);
	assert(input.buttons & SWITCH_PRO_BTN_LEFT);
	assert(input.buttons & SWITCH_PRO_BTN_RIGHT);
	assert(input.buttons & SWITCH_PRO_BTN_UP);
	assert(input.buttons & SWITCH_PRO_BTN_DOWN);
}

static void test_basic_report_and_first_packet_drop(void)
{
	struct SwitchProState state;
	struct SwitchProInput input;
	u8 report[12] = {
		SWITCH_PRO_REPORT_BASIC, 0x02 | 0x10 | 0x40,
		0x01, 0x01,
		0x00, 0x80, 0x00, 0x80,
		0xFF, 0xFF, 0x00, 0x00
	};

	SwitchProReset(&state);
	assert(SwitchProParseReport(&state, report, sizeof(report), &input) == 0);
	assert(SwitchProParseReport(&state, report, sizeof(report), &input) == 1);
	assert(input.left_x == 0 && input.left_y == 0);
	assert(input.right_x == 127 && input.right_y == 127);
	assert(input.buttons & SWITCH_PRO_BTN_A);
	assert(input.buttons & SWITCH_PRO_BTN_L);
	assert(input.buttons & SWITCH_PRO_BTN_ZL);
	assert(input.buttons & SWITCH_PRO_BTN_MINUS);
	assert(input.buttons & SWITCH_PRO_BTN_UP);
	assert(input.buttons & SWITCH_PRO_BTN_RIGHT);
}

static void test_full_button_bits_individually(void)
{
	struct ButtonCase {
		u8 offset;
		u8 bit;
		u32 expected;
	};
	static const struct ButtonCase cases[] = {
		{3, 0x08, SWITCH_PRO_BTN_A},
		{3, 0x04, SWITCH_PRO_BTN_B},
		{3, 0x02, SWITCH_PRO_BTN_X},
		{3, 0x01, SWITCH_PRO_BTN_Y},
		{3, 0x40, SWITCH_PRO_BTN_R},
		{3, 0x80, SWITCH_PRO_BTN_ZR},
		{4, 0x02, SWITCH_PRO_BTN_PLUS},
		{4, 0x01, SWITCH_PRO_BTN_MINUS},
		{4, 0x10, SWITCH_PRO_BTN_HOME},
		{5, 0x40, SWITCH_PRO_BTN_L},
		{5, 0x80, SWITCH_PRO_BTN_ZL},
		{5, 0x08, SWITCH_PRO_BTN_LEFT},
		{5, 0x04, SWITCH_PRO_BTN_RIGHT},
		{5, 0x02, SWITCH_PRO_BTN_UP},
		{5, 0x01, SWITCH_PRO_BTN_DOWN}
	};
	struct SwitchProState state;
	struct SwitchProInput input;
	u8 report[13];
	u32 i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		memset(report, 0, sizeof(report));
		SwitchProReset(&state);
		report[0] = SWITCH_PRO_REPORT_FULL;
		pack_axis(&report[6], 0x800, 0x800);
		pack_axis(&report[9], 0x800, 0x800);
		report[cases[i].offset] = cases[i].bit;
		assert(SwitchProParseReport(&state, report, sizeof(report),
			&input) == 1);
		assert(input.buttons == cases[i].expected);
	}
}

static void test_basic_button_bits_individually(void)
{
	struct ButtonCase {
		u8 offset;
		u8 bit;
		u32 expected;
	};
	static const struct ButtonCase cases[] = {
		{1, 0x02, SWITCH_PRO_BTN_A},
		{1, 0x01, SWITCH_PRO_BTN_B},
		{1, 0x08, SWITCH_PRO_BTN_X},
		{1, 0x04, SWITCH_PRO_BTN_Y},
		{1, 0x10, SWITCH_PRO_BTN_L},
		{1, 0x20, SWITCH_PRO_BTN_R},
		{1, 0x40, SWITCH_PRO_BTN_ZL},
		{1, 0x80, SWITCH_PRO_BTN_ZR},
		{2, 0x02, SWITCH_PRO_BTN_PLUS},
		{2, 0x01, SWITCH_PRO_BTN_MINUS},
		{2, 0x10, SWITCH_PRO_BTN_HOME}
	};
	struct SwitchProState state;
	struct SwitchProInput input;
	u8 report[12];
	u32 i;

	for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		memset(report, 0, sizeof(report));
		SwitchProReset(&state);
		report[0] = SWITCH_PRO_REPORT_BASIC;
		report[3] = 8;
		report[5] = report[7] = report[9] = report[11] = 0x80;
		report[cases[i].offset] = cases[i].bit;
		assert(SwitchProParseReport(&state, report, sizeof(report),
			&input) == 0);
		assert(SwitchProParseReport(&state, report, sizeof(report),
			&input) == 1);
		assert(input.buttons == cases[i].expected);
	}
}

static void test_basic_dpad_and_axis_endianness(void)
{
	static const u32 expected_dpad[] = {
		SWITCH_PRO_BTN_UP,
		SWITCH_PRO_BTN_UP | SWITCH_PRO_BTN_RIGHT,
		SWITCH_PRO_BTN_RIGHT,
		SWITCH_PRO_BTN_DOWN | SWITCH_PRO_BTN_RIGHT,
		SWITCH_PRO_BTN_DOWN,
		SWITCH_PRO_BTN_DOWN | SWITCH_PRO_BTN_LEFT,
		SWITCH_PRO_BTN_LEFT,
		SWITCH_PRO_BTN_UP | SWITCH_PRO_BTN_LEFT,
		0
	};
	struct SwitchProState state;
	struct SwitchProInput input;
	u8 report[12];
	u32 i;

	for(i = 0; i < sizeof(expected_dpad) / sizeof(expected_dpad[0]); i++)
	{
		memset(report, 0, sizeof(report));
		SwitchProReset(&state);
		report[0] = SWITCH_PRO_REPORT_BASIC;
		report[3] = i;
		/* Little-endian axes: left is right/up, right is left/down. */
		report[4] = 0xFF; report[5] = 0xFF;
		report[6] = 0x00; report[7] = 0x00;
		report[8] = 0x00; report[9] = 0x00;
		report[10] = 0xFF; report[11] = 0xFF;
		assert(SwitchProParseReport(&state, report, sizeof(report),
			&input) == 0);
		assert(SwitchProParseReport(&state, report, sizeof(report),
			&input) == 1);
		assert(input.buttons == expected_dpad[i]);
		assert(input.left_x == 127 && input.left_y == 127);
		assert(input.right_x == -128 && input.right_y == -127);
	}
}

static void test_subcommand(void)
{
	struct SwitchProState state;
	u8 report[16];
	u8 mode = SWITCH_PRO_REPORT_FULL;
	u16 len;

	SwitchProReset(&state);
	len = SwitchProBuildSubcommand(&state, report, sizeof(report),
		SWITCH_PRO_SUBCMD_REPORT_MODE, &mode, 1);
	assert(len == 12);
	assert(report[0] == 0x01);
	assert(report[1] == 0x00);
	assert(report[2] == 0x00 && report[3] == 0x00);
	assert(report[4] == 0x00 && report[5] == 0x00);
	assert(report[6] == 0x00 && report[7] == 0x00);
	assert(report[8] == 0x00 && report[9] == 0x00);
	assert(report[10] == SWITCH_PRO_SUBCMD_REPORT_MODE);
	assert(report[11] == SWITCH_PRO_REPORT_FULL);

	len = SwitchProBuildSubcommand(&state, report, sizeof(report),
		SWITCH_PRO_SUBCMD_DEVICE_INFO, NULL, 0);
	assert(len == 11);
	assert(report[1] == 0x01);
	assert(report[10] == SWITCH_PRO_SUBCMD_DEVICE_INFO);
}

static void test_diagnostic_leds(void)
{
	assert(SwitchProDiagnosticLED(0, 0) == 0x00);
	assert(SwitchProDiagnosticLED(1, 0) == 0x10);
	assert(SwitchProDiagnosticLED(2, 0) == 0x30);
	assert(SwitchProDiagnosticLED(3, 0) == 0x70);
	assert(SwitchProDiagnosticLED(4, 0) == 0xF0);
	assert(SwitchProDiagnosticLED(5, 0) == 0xA0);
	assert(SwitchProDiagnosticLED(5, 1) == 0x50);
	assert(SwitchProDiagnosticLED(6, 0) == 0x00);
	assert(SwitchProDiagnosticLED(6, 1) == 0xF0);
	assert(SwitchProDiagnosticLED(7, 0) == 0x00);
	assert(SwitchProDiagnosticLED(7, 1) == 0xF0);
	assert(SwitchProDiagnosticLED(8, 0) == 0x00);
	assert(SwitchProDiagnosticLED(8, 1) == 0xF0);
	assert(SwitchProDiagnosticLED(9, 0) == 0x60);
	assert(SwitchProDiagnosticLED(9, 1) == 0x90);
	assert(SwitchProDiagnosticLED(10, 0) == 0xC0);
	assert(SwitchProDiagnosticLED(10, 1) == 0x30);
	assert(SwitchProDiagnosticLED(11, 0) == 0x90);
	assert(SwitchProDiagnosticLED(11, 1) == 0x90);
	assert(SwitchProDiagnosticLED(12, 0) == 0x60);
	assert(SwitchProDiagnosticLED(12, 1) == 0x60);
}

static void test_init_happy_path(void)
{
	struct SwitchProState state;
	u8 action;
	u8 device_info[3] = {0, 0, 3};
	u8 user_cal[27];
	u8 factory_cal[23];

	SwitchProReset(&state);
	SwitchProInitStart(&state);
	assert(state.init_state == SWITCH_PRO_INIT_INITIAL_DELAY);
	assert(SwitchProInitDelayMs(&state) == 15);
	action = SwitchProInitPoll(&state);
	assert(action == SWITCH_PRO_INIT_ACTION_DEVICE_INFO);
	assert(state.init_state == SWITCH_PRO_INIT_WAIT_DEVICE_INFO);
	assert(state.init_retries == 1);
	assert(SwitchProInitDelayMs(&state) == 100);

	/* An unrelated acknowledgement must not advance the state. */
	assert(SwitchProInitHandleResponse(&state, 0x80,
		SWITCH_PRO_SUBCMD_PLAYER_LED, NULL, 0) == SWITCH_PRO_ACK_IGNORED);
	assert(state.init_state == SWITCH_PRO_INIT_WAIT_DEVICE_INFO);

	assert(SwitchProInitHandleResponse(&state, 0x82,
		SWITCH_PRO_SUBCMD_DEVICE_INFO, device_info,
		sizeof(device_info)) == SWITCH_PRO_ACK_ACCEPTED);
	assert(state.device_type == 3);
	assert(state.init_state == SWITCH_PRO_INIT_DEVICE_INFO_ACKED);
	assert(SwitchProInitDelayMs(&state) == 10);
	action = SwitchProInitPoll(&state);
	assert(action == SWITCH_PRO_INIT_ACTION_PLAYER_LED);
	assert(state.init_state == SWITCH_PRO_INIT_WAIT_PLAYER_LED);
	assert(SwitchProInitHandleResponse(&state, 0x80,
		SWITCH_PRO_SUBCMD_PLAYER_LED, NULL, 0) == SWITCH_PRO_ACK_ACCEPTED);
	assert(state.init_state == SWITCH_PRO_INIT_PLAYER_LED_ACKED);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_VIBRATION);
	assert(SwitchProInitHandleResponse(&state, 0x80,
		SWITCH_PRO_SUBCMD_VIBRATION, NULL, 0) == SWITCH_PRO_ACK_ACCEPTED);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_USER_CAL);

	/* No valid user calibration: proceed to the factory block. */
	memset(user_cal, 0, sizeof(user_cal));
	user_cal[0] = 0x10; user_cal[1] = 0x80;
	user_cal[4] = 22;
	assert(SwitchProInitHandleResponse(&state, 0x90,
		SWITCH_PRO_SUBCMD_SPI_READ, user_cal,
		sizeof(user_cal)) == SWITCH_PRO_ACK_ACCEPTED);
	assert(!state.left_calibrated && !state.right_calibrated);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_FACTORY_CAL);

	memset(factory_cal, 0, sizeof(factory_cal));
	factory_cal[0] = 0x3D; factory_cal[1] = 0x60;
	factory_cal[4] = 18;
	/* All-zero calibration is syntactically valid and exercises parsing. */
	assert(SwitchProInitHandleResponse(&state, 0x90,
		SWITCH_PRO_SUBCMD_SPI_READ, factory_cal,
		sizeof(factory_cal)) == SWITCH_PRO_ACK_ACCEPTED);
	assert(state.left_calibrated && state.right_calibrated);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_REPORT_MODE);
	assert(SwitchProInitHandleResponse(&state, 0x80,
		SWITCH_PRO_SUBCMD_REPORT_MODE, NULL, 0) == SWITCH_PRO_ACK_ACCEPTED);
	assert(state.init_state == SWITCH_PRO_INIT_READY);
	assert(SwitchProInitDelayMs(&state) == 0);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_NONE);
}

static void test_init_retry_timeout_and_negative_ack(void)
{
	struct SwitchProState state;
	u8 i;

	SwitchProReset(&state);
	SwitchProInitStart(&state);
	assert(SwitchProInitPoll(&state) ==
		SWITCH_PRO_INIT_ACTION_DEVICE_INFO);
	assert(SwitchProInitHandleResponse(&state, 0x00,
		SWITCH_PRO_SUBCMD_DEVICE_INFO, NULL, 0) == SWITCH_PRO_ACK_NEGATIVE);
	assert(state.init_state == SWITCH_PRO_INIT_WAIT_DEVICE_INFO);

	for(i = 1; i < SWITCH_PRO_INIT_RETRY_MAX; i++)
		assert(SwitchProInitPoll(&state) ==
			SWITCH_PRO_INIT_ACTION_DEVICE_INFO);
	assert(state.init_retries == SWITCH_PRO_INIT_RETRY_MAX);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_FAILED);
	assert(state.init_state == SWITCH_PRO_INIT_FAILED);
	assert(SwitchProInitPoll(&state) == SWITCH_PRO_INIT_ACTION_NONE);
}

static void test_stream_readiness_excludes_command_replies(void)
{
	struct SwitchProState state;

	SwitchProReset(&state);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_COMMAND, 1) == 0);
	assert(state.consecutive_stream_reports == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 1) == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_COMMAND, 1) == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 0) == 0);
	assert(state.consecutive_stream_reports == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 1) == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 1) == 0);
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 1) == 1);
	assert(state.consecutive_stream_reports ==
		SWITCH_PRO_STREAM_READY_REPORTS);
	/* Once streaming is proven, one malformed packet must not unassign the pad. */
	assert(SwitchProTrackStreamReport(&state,
		SWITCH_PRO_REPORT_BASIC, 0) == 1);
	assert(state.consecutive_stream_reports ==
		SWITCH_PRO_STREAM_READY_REPORTS);
}

static void test_incoming_transport_sequence(void)
{
	struct SwitchProTransport transport;
	SwitchProTransportBegin(&transport, SWITCH_PRO_CONNECTION_INCOMING);
	assert(transport.state == SWITCH_PRO_TRANSPORT_WAIT_ACL);
	assert(SwitchProTransportACLReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportSecurityReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_ACCEPT_HID);
	assert(transport.state == SWITCH_PRO_TRANSPORT_WAIT_INCOMING_HID);
	assert(SwitchProTransportChannelReady(&transport, 1) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportChannelReady(&transport, 0) ==
		SWITCH_PRO_TRANSPORT_ACTION_READY);
	assert(transport.state == SWITCH_PRO_TRANSPORT_READY);
}

static void test_outgoing_transport_sequence(void)
{
	struct SwitchProTransport transport;
	SwitchProTransportBegin(&transport, SWITCH_PRO_CONNECTION_OUTGOING);
	assert(SwitchProTransportACLReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportSecurityReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_OPEN_HID);
	assert(transport.state == SWITCH_PRO_TRANSPORT_OPEN_OUTGOING_HID);
	assert(SwitchProTransportChannelReady(&transport, 1) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportChannelReady(&transport, 0) ==
		SWITCH_PRO_TRANSPORT_ACTION_READY);
}

static void test_transport_rejects_wrong_order(void)
{
	struct SwitchProTransport transport;
	SwitchProTransportReset(&transport);
	assert(SwitchProTransportSecurityReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	SwitchProTransportBegin(&transport, SWITCH_PRO_CONNECTION_INCOMING);
	assert(SwitchProTransportChannelReady(&transport, 1) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(transport.state == SWITCH_PRO_TRANSPORT_WAIT_ACL);
	SwitchProTransportFail(&transport);
	assert(transport.state == SWITCH_PRO_TRANSPORT_FAILED);
}

static void test_incoming_channels_may_arrive_before_security(void)
{
	struct SwitchProTransport transport;
	SwitchProTransportBegin(&transport, SWITCH_PRO_CONNECTION_INCOMING);
	assert(SwitchProTransportChannelReady(&transport, 1) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportChannelReady(&transport, 0) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportACLReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_NONE);
	assert(SwitchProTransportSecurityReady(&transport) ==
		SWITCH_PRO_TRANSPORT_ACTION_READY);
	assert(transport.state == SWITCH_PRO_TRANSPORT_READY);
}

int main(void)
{
	test_full_report();
	test_basic_report_and_first_packet_drop();
	test_full_button_bits_individually();
	test_basic_button_bits_individually();
	test_basic_dpad_and_axis_endianness();
	test_subcommand();
	test_diagnostic_leds();
	test_init_happy_path();
	test_init_retry_timeout_and_negative_ack();
	test_stream_readiness_excludes_command_replies();
	test_incoming_transport_sequence();
	test_outgoing_transport_sequence();
	test_transport_rejects_wrong_order();
	test_incoming_channels_may_arrive_before_security();
	puts("switch_pro tests: ok");
	return 0;
}
