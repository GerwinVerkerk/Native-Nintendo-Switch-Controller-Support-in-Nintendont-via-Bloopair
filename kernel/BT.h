/*
BT.h for Nintendont (Kernel)

Copyright (C) 2014 FIX94

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#ifndef _BT_H_
#define _BT_H_

#include "lwbt/bte.h"
#include "SwitchPro.h"

void BTInit(void);
void BTUpdateRegisters(void);
void BTTraceDumpToFile(void);

#define BT_DIAG_FOUND             1
#define BT_DIAG_SSP_COMPLETE      2
#define BT_DIAG_LINK_KEY_STORED   3
#define BT_DIAG_HID_OPEN          4
#define BT_DIAG_AUTHENTICATED     5
#define BT_DIAG_ENCRYPTED         6
#define BT_DIAG_PROTOCOL_READY    7
#define BT_DIAG_INPUT_RECEIVED    8
#define BT_DIAG_AUTH_FAILED       9
#define BT_DIAG_ENCRYPT_FAILED   10
#define BT_DIAG_AUTH_REQUESTED   11
#define BT_DIAG_PPC_SELFTEST     12

#define BT_HID_HOST_ACL_REQUEST       1
#define BT_HID_HOST_CONTROL_REQUEST   2
#define BT_HID_HOST_CONTROL_COMPLETE  3
#define BT_HID_HOST_INTERRUPT_REQUEST 4
#define BT_HID_HOST_INTERRUPT_COMPLETE 5
#define BT_HID_HOST_SLOT_REUSED        6
#define BT_HID_HOST_SLOT_CREATED       7
#define BT_HID_HOST_ACL_RETRY          8
#define BT_HID_HOST_INCOMING_REQUEST   9
#define BT_HID_HOST_INCOMING_LISTEN   10
#define BT_HID_HOST_REMOTE_NAME       11
#define BT_HID_HOST_TRANSPORT_READY   12
#define BT_HID_HOST_SLOT_PROMOTED     13

void BTDiagnosticPairingPhase(u32 phase, const struct bd_addr *bdaddr);
void BTDiagnosticLinkKeyQueued(const struct bd_addr *bdaddr);
void BTDiagnosticCacheLinkKey(const struct bd_addr *bdaddr, const u8 *key);
u8 BTDiagnosticGetLinkKey(const struct bd_addr *bdaddr, u8 *key);
void BTDiagnosticLinkKeyStoreResult(u8 result);
void BTDiagnosticAuthenticationCommandResult(u8 result);
void BTDiagnosticAuthenticationResult(u8 result, const struct bd_addr *bdaddr);
void BTDiagnosticEncryptionResult(u8 result, u8 enabled,
	const struct bd_addr *bdaddr);
void BTDiagnosticHIDChannelsOpen(const struct bd_addr *bdaddr);
void BTDiagnosticConnectionTarget(const struct bd_addr *bdaddr);
void BTDiagnosticIncomingConnectionRequest(const struct bd_addr *bdaddr,
	const u8 *cod);
void BTDiagnosticACLResult(const struct bd_addr *bdaddr, u32 result);
void BTDiagnosticHIDHostEvent(const struct bd_addr *bdaddr, u32 stage,
	u32 result, u32 status);
void BTDiagnosticHIDChannelOpen(const struct bd_addr *bdaddr,
	u8 control_channel);
void BTDiagnosticRemoteNameResult(const struct bd_addr *bdaddr, u8 result,
	const u8 *name, u16 length);

struct BTPadStat {
	u32 controller;
	u32 timeout;
	u32 transfertype;
	u32 transferstate;
	u32 channel;
	u32 rumble;
	u32 rumbletime;
	u32 diagnostic_state;
	s16 xAxisLmid;
	s16 xAxisRmid;
	s16 yAxisLmid;
	s16 yAxisRmid;
	struct bte_pcb *sock;
	struct bd_addr bdaddr;
	struct SwitchProState switch_state;
	u32 switch_init_timer;
	struct SwitchProInput switch_input;
	u32 switch_input_valid;
	u32 switch_input_reports;
	u32 switch_publish_count;
	u32 switch_led_channel;
	u32 switch_selftest_state;
	u32 switch_selftest_timer;
	struct SwitchProTransport switch_transport;
	u8 switch_link_key[16];
	u8 switch_link_key_valid;
	u8 switch_link_key_store_pending;
	u8 switch_remote_name_requested;
	u8 switch_remote_name_verified;
} ALIGNED(32);

struct BTPadCont {
	u32 used;
	s16 xAxisL;
	s16 xAxisR;
	s16 yAxisL;
	s16 yAxisR;
	u32 button;
	u8 triggerL;
	u8 triggerR;
	s16 xAccel;
	s16 yAccel;
	s16 zAccel;
} ALIGNED(32);

typedef char BTPadContSizeCheck[(sizeof(struct BTPadCont) == 32) ? 1 : -1];
typedef char BTPadContButtonOffsetCheck[(__builtin_offsetof(struct BTPadCont, button) == 12) ? 1 : -1];

#define BT_DPAD_UP              0x0001
#define BT_DPAD_LEFT            0x0002
#define BT_TRIGGER_ZR           0x0004
#define BT_BUTTON_X             0x0008
#define BT_BUTTON_A             0x0010
#define BT_BUTTON_Y             0x0020
#define BT_BUTTON_B             0x0040
#define BT_TRIGGER_ZL           0x0080
#define BT_TRIGGER_R            0x0200
#define BT_BUTTON_START         0x0400
#define BT_BUTTON_HOME          0x0800
#define BT_BUTTON_SELECT        0x1000
#define BT_TRIGGER_L            0x2000
#define BT_DPAD_DOWN            0x4000
#define BT_DPAD_RIGHT           0x8000


#define WM_BUTTON_TWO			0x0001
#define WM_BUTTON_ONE			0x0002
#define WM_BUTTON_B				0x0004
#define WM_BUTTON_A				0x0008
#define WM_BUTTON_MINUS			0x0010
#define NUN_BUTTON_Z			0x0020 
#define NUN_BUTTON_C			0x0040
#define WM_BUTTON_HOME			0x0080
#define WM_BUTTON_LEFT			0x0100
#define WM_BUTTON_RIGHT			0x0200
#define WM_BUTTON_DOWN			0x0400
#define WM_BUTTON_UP			0x0800
#define WM_BUTTON_PLUS			0x1000

/* From LibOGC conf.h */

#define CONF_PAD_MAX_REGISTERED 10
#define CONF_PAD_MAX_ACTIVE 4

typedef struct _conf_pad_device conf_pad_device;

struct _conf_pad_device {
	u8 bdaddr[6];
	char name[0x40];
} __attribute__((packed));

typedef struct _conf_pads conf_pads;

struct _conf_pads {
	u8 num_registered;
	conf_pad_device registered[CONF_PAD_MAX_REGISTERED];
	conf_pad_device active[CONF_PAD_MAX_ACTIVE];
	conf_pad_device balance_board;
	conf_pad_device unknown;
} __attribute__((packed));

#endif
