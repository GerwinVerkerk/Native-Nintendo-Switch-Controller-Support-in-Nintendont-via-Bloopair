#ifndef _SWITCH_PRO_DIAG_H_
#define _SWITCH_PRO_DIAG_H_

#define SWITCH_PRO_DIAG_MAGIC 0x53575052
#define SWITCH_PRO_DIAG_VERSION 1
#define SWITCH_PRO_DIAG_REPORTS 4

#define SWITCH_PRO_DIAG_ARM_ADDR 0x132F0100
#define SWITCH_PRO_DIAG_PPC_ADDR 0x132F0200

struct SwitchProDiagReport {
	u32 sequence;
	u32 report_id_len;
	u32 buttons;
	s16 left_x;
	s16 left_y;
	s16 right_x;
	s16 right_y;
	u8 raw[12];
} __attribute__((aligned(32)));

struct SwitchProArmDiag {
	u32 magic;
	u32 version;
	u32 report_sequence;
	u32 publish_sequence;
	u32 publish_channel;
	u32 publish_used;
	u32 publish_buttons;
	s16 publish_left_x;
	s16 publish_left_y;
	s16 publish_right_x;
	s16 publish_right_y;
	u32 selftest_state;
	u32 reserved[5];
	struct SwitchProDiagReport reports[SWITCH_PRO_DIAG_REPORTS];
} __attribute__((aligned(32)));

struct SwitchProPpcDiag {
	u32 magic;
	u32 version;
	u32 read_sequence;
	u32 channel;
	u32 seen_used;
	u32 seen_buttons;
	s16 seen_left_x;
	s16 seen_left_y;
	s16 seen_right_x;
	s16 seen_right_y;
	u32 pad_buttons;
	s8 pad_stick_x;
	s8 pad_stick_y;
	s8 pad_substick_x;
	s8 pad_substick_y;
	u32 selftest_a_seen;
	u32 reserved[5];
} __attribute__((aligned(32)));

typedef char SwitchProDiagReportSize[(sizeof(struct SwitchProDiagReport) == 32) ? 1 : -1];
typedef char SwitchProArmDiagSize[(sizeof(struct SwitchProArmDiag) == 192) ? 1 : -1];
typedef char SwitchProPpcDiagSize[(sizeof(struct SwitchProPpcDiag) == 64) ? 1 : -1];

#endif
