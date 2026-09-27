/*
 * Minimal Nintendo Switch Pro Controller protocol support for Nintendont.
 *
 * Report layout and initialization sequence are based on Bloopair's
 * switch_controller implementation (GPL-2.0-or-later) and the public
 * Nintendo Switch reverse-engineering documentation referenced there.
 */
#ifndef _SWITCH_PRO_H_
#define _SWITCH_PRO_H_

#ifdef SWITCH_PRO_HOST_TEST
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
#else
#include "global.h"
#endif

#define SWITCH_PRO_REPORT_COMMAND 0x21
#define SWITCH_PRO_REPORT_FULL    0x30
#define SWITCH_PRO_REPORT_BASIC   0x3F

#define SWITCH_PRO_SUBCMD_DEVICE_INFO 0x02
#define SWITCH_PRO_SUBCMD_REPORT_MODE 0x03
#define SWITCH_PRO_SUBCMD_SPI_READ    0x10
#define SWITCH_PRO_SUBCMD_PLAYER_LED  0x30
#define SWITCH_PRO_SUBCMD_VIBRATION   0x48

#define SWITCH_PRO_FACTORY_CAL_ADDR 0x603D
#define SWITCH_PRO_USER_CAL_ADDR    0x8010

#define SWITCH_PRO_INIT_RETRY_MAX 10
#define SWITCH_PRO_STREAM_READY_REPORTS 3

enum SwitchProInitState {
	SWITCH_PRO_INIT_IDLE = 0,
	SWITCH_PRO_INIT_INITIAL_DELAY,
	SWITCH_PRO_INIT_WAIT_DEVICE_INFO,
	SWITCH_PRO_INIT_DEVICE_INFO_ACKED,
	SWITCH_PRO_INIT_WAIT_PLAYER_LED,
	SWITCH_PRO_INIT_PLAYER_LED_ACKED,
	SWITCH_PRO_INIT_WAIT_VIBRATION,
	SWITCH_PRO_INIT_VIBRATION_ACKED,
	SWITCH_PRO_INIT_WAIT_USER_CAL,
	SWITCH_PRO_INIT_USER_CAL_ACKED,
	SWITCH_PRO_INIT_WAIT_FACTORY_CAL,
	SWITCH_PRO_INIT_FACTORY_CAL_ACKED,
	SWITCH_PRO_INIT_WAIT_REPORT_MODE,
	SWITCH_PRO_INIT_READY,
	SWITCH_PRO_INIT_FAILED
};

enum SwitchProInitAction {
	SWITCH_PRO_INIT_ACTION_NONE = 0,
	SWITCH_PRO_INIT_ACTION_DEVICE_INFO,
	SWITCH_PRO_INIT_ACTION_PLAYER_LED,
	SWITCH_PRO_INIT_ACTION_VIBRATION,
	SWITCH_PRO_INIT_ACTION_USER_CAL,
	SWITCH_PRO_INIT_ACTION_FACTORY_CAL,
	SWITCH_PRO_INIT_ACTION_REPORT_MODE,
	SWITCH_PRO_INIT_ACTION_FAILED
};

enum SwitchProAckResult {
	SWITCH_PRO_ACK_IGNORED = 0,
	SWITCH_PRO_ACK_ACCEPTED,
	SWITCH_PRO_ACK_NEGATIVE
};

#define SWITCH_PRO_BTN_UP       0x0001
#define SWITCH_PRO_BTN_LEFT     0x0002
#define SWITCH_PRO_BTN_ZR       0x0004
#define SWITCH_PRO_BTN_X        0x0008
#define SWITCH_PRO_BTN_A        0x0010
#define SWITCH_PRO_BTN_Y        0x0020
#define SWITCH_PRO_BTN_B        0x0040
#define SWITCH_PRO_BTN_ZL       0x0080
#define SWITCH_PRO_BTN_R        0x0200
#define SWITCH_PRO_BTN_PLUS     0x0400
#define SWITCH_PRO_BTN_HOME     0x0800
#define SWITCH_PRO_BTN_MINUS    0x1000
#define SWITCH_PRO_BTN_L        0x2000
#define SWITCH_PRO_BTN_DOWN     0x4000
#define SWITCH_PRO_BTN_RIGHT    0x8000

struct SwitchProState {
	u8 drop_first_basic_report;
	u8 report_counter;
	u8 init_state;
	u8 init_retries;
	u8 pending_subcommand;
	u8 consecutive_stream_reports;
	u8 device_type;
	u8 left_calibrated;
	u8 right_calibrated;
	u16 left_center_x;
	u16 left_center_y;
	u16 left_min_x;
	u16 left_min_y;
	u16 left_max_x;
	u16 left_max_y;
	u16 right_center_x;
	u16 right_center_y;
	u16 right_min_x;
	u16 right_min_y;
	u16 right_max_x;
	u16 right_max_y;
};

struct SwitchProInput {
	s16 left_x;
	s16 left_y;
	s16 right_x;
	s16 right_y;
	u32 buttons;
};

void SwitchProReset(struct SwitchProState *state);
s32 SwitchProParseReport(struct SwitchProState *state, const u8 *report,
	u16 len, struct SwitchProInput *input);
u16 SwitchProBuildSubcommand(struct SwitchProState *state, u8 *report,
	u16 capacity, u8 command, const u8 *data, u8 data_len);
void SwitchProInitStart(struct SwitchProState *state);
u16 SwitchProInitDelayMs(const struct SwitchProState *state);
u8 SwitchProInitPoll(struct SwitchProState *state);
u8 SwitchProInitHandleResponse(struct SwitchProState *state, u8 ack, u8 command,
	const u8 *data, u16 data_len);
u8 SwitchProTrackStreamReport(struct SwitchProState *state, u8 report_id,
	u8 parsed);
u8 SwitchProDiagnosticLED(u32 phase, u8 blink_on);

#endif
