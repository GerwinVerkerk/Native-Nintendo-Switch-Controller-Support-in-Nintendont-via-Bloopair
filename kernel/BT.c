/*
BT.c for Nintendont (Kernel)

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

/* Wiimote and Extension Documentation from wiibrew.org */
/* WiiU Pro Controller Documentation from TeHaxor69 */
/* lwBT ported from LibOGC */

#include "global.h"
#include "string.h"
#include "BT.h"
#include "lwbt/btarch.h"
#include "lwbt/hci.h"
#include "lwbt/l2cap.h"
#include "lwbt/physbusif.h"
#include "Config.h"
#include "SwitchProIncoming.h"
#include "../common/include/SwitchProPairing.h"
#include "../common/include/SwitchProMinimalStatus.h"

extern int dbgprintf( const char *fmt, ...);

static vu32 BTChannelsUsed = 0;
extern vu32 intr, bulk;

static conf_pads *BTDevices = (conf_pads*)0x132C0000;
static struct BTPadStat *BTPadConnected[4];

static struct BTPadStat BTPadStatus[CONF_PAD_MAX_REGISTERED] ALIGNED(32);
static struct BTPadStat SwitchPadStatus ALIGNED(32);
static struct SwitchProIncomingState SwitchIncoming ALIGNED(32);
static SwitchProPairing SwitchPairing ALIGNED(32);
static struct bd_addr SwitchAddress;
static u8 BTStackReady = 0;
static u32 SwitchInitTimer = 0;
static u32 SwitchTransportTimer = 0;
static SwitchProMinimalStatus *SwitchStatus =
	(SwitchProMinimalStatus*)SWITCH_PRO_STATUS_ARM_ADDR;

static struct BTPadCont *BTPad = (struct BTPadCont*)0x132F0000;

static vu32* BTMotor = (u32*)0x13003040;
static vu32* BTPadFree = (u32*)0x13003050;

static vu32* IRSensitivity = (u32*)0x132C0490;
static vu32* SensorBarPosition = (u32*)0x132C0494;

static const u8 LEDState[] = { 0x10, 0x20, 0x40, 0x80, 0xF0 };

#define CHAN_NOT_SET 4
#define SWITCH_PRO_INIT_RETRY_TICKS 189843
#define SWITCH_PRO_TRANSPORT_TIMEOUT_SECONDS 2

#define TRANSFER_CONNECT 0
#define TRANSFER_EXT1 1
#define TRANSFER_EXT2 2
#define TRANSFER_SET_IDENT 3
#define TRANSFER_GET_IDENT 4
#define TRANSFER_ENABLE_IR_PIXEL_CLOCK 5
#define TRANSFER_ENABLE_IR_LOGIC 6
#define TRANSFER_WRITE_IR_REG30_1 7
#define TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_1 8
#define TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_2 9
#define TRANSFER_WRITE_IR_MODE 10
#define TRANSFER_WRITE_IR_REG30_8 11
#define TRANSFER_CALIBRATE 12
#define TRANSFER_DONE 13

#define C_NOT_SET	(0<<0)
#define C_CCP		(1<<0)
#define C_CC		(1<<1)
#define C_SWAP		(1<<2)
#define C_RUMBLE_WM	(1<<3)
#define C_NUN		(1<<4)
#define C_NSWAP1	(1<<5)
#define C_NSWAP2	(1<<6)
#define C_NSWAP3	(1<<7)
#define C_ISWAP		(1<<8)
#define C_TestSWAP	(1<<9)

typedef char SwitchProControllerContractCheck[
	(C_CCP == SWITCH_PRO_CANONICAL_CONTROLLER) ? 1 : -1];
typedef char SwitchProListenerCapacityCheck[
	(MEMB_NUM_L2CAP_PCB_LISTEN >=
	2 * (CONF_PAD_MAX_REGISTERED + 1)) ? 1 : -1];

static const s8 DEADZONE = 0x1A;

static u8 IsSwitchPad(const struct BTPadStat *stat)
{
	return stat == &SwitchPadStatus;
}

static u8 IsSwitchAddress(const struct bd_addr *bdaddr)
{
	return SwitchIncoming.imported && bdaddr != NULL &&
		bd_addr_cmp(&SwitchAddress,bdaddr);
}

static void SetSwitchStatusFlag(u32 flag)
{
	SwitchStatus->flags |= flag;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
}

static void SetSwitchStatusError(s32 error)
{
	if(error != ERR_OK)
		SwitchStatus->last_error = error;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
}

static s32 BTSendSwitchInit(u8 retry)
{
	u8 report[16];
	u16 len = SwitchProIncomingBuildInit(&SwitchIncoming, report,
		sizeof(report), retry);
	s32 result;

	if(len == 0)
	{
		if(SwitchIncoming.init_failed)
			SetSwitchStatusFlag(SWITCH_PRO_STATUS_INIT_FAILED);
		return ERR_VAL;
	}
	SwitchInitTimer = read32(HW_TIMER);
	SwitchStatus->init_send_attempts++;
	result = bte_senddata(SwitchPadStatus.sock, report, len);
	SwitchStatus->init_last_send_result = result;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
	if(result != ERR_OK)
	{
		SetSwitchStatusError(result);
		return result;
	}
	SwitchStatus->init_sent = SwitchIncoming.init_sent;
	SwitchStatus->init_retries = SwitchIncoming.init_retries;
	SwitchStatus->init_index = SwitchIncoming.init_index;
	if(retry)
		SwitchStatus->flags |= SWITCH_PRO_STATUS_INIT_RETRIED;
	if(SwitchIncoming.pending_subcommand == 0x03)
		SwitchStatus->flags |= SWITCH_PRO_STATUS_REPORT_MODE;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
	return ERR_OK;
}

static s32 BTTryFinalizeSwitchConnection(void)
{
	if(SwitchIncoming.finalized)
		return ERR_OK;
	if(!SwitchProIncomingNeedsFinalize(&SwitchIncoming))
		return ERR_CONN;
	if(BTChannelsUsed >= 4)
		return ERR_USE;

	SwitchProIncomingFinalized(&SwitchIncoming);
	SwitchPadStatus.channel = CHAN_NOT_SET;
	SwitchPadStatus.rumble = 0;
	SwitchPadStatus.transferstate = TRANSFER_DONE;
	SwitchPadStatus.controller = C_CCP;
	BTPadConnected[BTChannelsUsed++] = &SwitchPadStatus;
	sync_after_write(&SwitchPadStatus, sizeof(SwitchPadStatus));
	SetSwitchStatusFlag(SWITCH_PRO_STATUS_CONNECTED);

	SwitchProIncomingStartInit(&SwitchIncoming);
	SetSwitchStatusFlag(SWITCH_PRO_STATUS_INIT_STARTED);
	return BTSendSwitchInit(0);
}

static void BTPumpSwitchConnection(void)
{
	u32 bte_state = 0;
	u32 control_state = L2CAP_CLOSED;
	u32 data_state = L2CAP_CLOSED;
	u8 transport_ready;

	if(!SwitchProIncomingReady(&SwitchIncoming) || SwitchIncoming.finalized ||
		SwitchIncoming.init_failed)
		return;

	SwitchStatus->transport_checks++;
	transport_ready = bte_ready_for_data(SwitchPadStatus.sock,&bte_state,
		&control_state,&data_state);
	SwitchStatus->bte_state = bte_state;
	SwitchStatus->control_l2cap_state = control_state;
	SwitchStatus->interrupt_l2cap_state = data_state;
	SwitchProIncomingTransport(&SwitchIncoming,transport_ready);
	if(!transport_ready)
	{
		SwitchStatus->transport_deferred++;
		SwitchStatus->flags |= SWITCH_PRO_STATUS_TRANSPORT_PENDING;
		if(SwitchTransportTimer == 0)
			SwitchTransportTimer = read32(HW_TIMER);
		else if(TimerDiffSeconds(SwitchTransportTimer) >=
			SWITCH_PRO_TRANSPORT_TIMEOUT_SECONDS)
		{
			SwitchIncoming.init_failed = 1;
			SwitchStatus->transport_timeouts++;
			SwitchStatus->flags |= SWITCH_PRO_STATUS_TRANSPORT_TIMEOUT |
				SWITCH_PRO_STATUS_INIT_FAILED;
			SetSwitchStatusError(ERR_CONN);
		}
		sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
		return;
	}

	SwitchTransportTimer = 0;
	SwitchStatus->flags |= SWITCH_PRO_STATUS_TRANSPORT_READY;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
	BTTryFinalizeSwitchConnection();
}

static void PublishSwitchInput(void)
{
	u32 chan = SwitchPadStatus.channel;
	if(chan >= CHAN_NOT_SET || !SwitchIncoming.input_valid)
		return;
	sync_before_read(&BTPad[chan], sizeof(struct BTPadCont));
	BTPad[chan].xAxisL = SwitchIncoming.input.left_x;
	BTPad[chan].yAxisL = SwitchIncoming.input.left_y;
	BTPad[chan].xAxisR = SwitchIncoming.input.right_x;
	BTPad[chan].yAxisR = SwitchIncoming.input.right_y;
	BTPad[chan].button = SwitchIncoming.input.buttons;
	BTPad[chan].triggerL =
		(SwitchIncoming.input.buttons & SWITCH_PRO_BTN_ZL) ? 0xff : 0;
	BTPad[chan].triggerR =
		(SwitchIncoming.input.buttons & SWITCH_PRO_BTN_ZR) ? 0xff : 0;
	BTPad[chan].used = C_CCP;
	SwitchStatus->publishes++;
	SwitchStatus->channel = chan;
	SwitchStatus->flags |= SWITCH_PRO_STATUS_PUBLISHED;
	sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
}

static void ImportSwitchPairing(void)
{
	SwitchProPairing *shared =
		(SwitchProPairing*)SWITCH_PRO_PAIRING_ARM_ADDR;
	u8 address[6];

	SwitchProIncomingReset(&SwitchIncoming);
	memset(SwitchStatus, 0, sizeof(*SwitchStatus));
	SwitchStatus->magic = SWITCH_PRO_STATUS_MAGIC;
	SwitchStatus->version = SWITCH_PRO_STATUS_VERSION;
	SwitchStatus->size = sizeof(*SwitchStatus);
	SwitchStatus->channel = CHAN_NOT_SET;
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
	memset(&SwitchPairing, 0, sizeof(SwitchPairing));
	sync_before_read(shared, sizeof(*shared));
	if(!SwitchProPairingIsValid(shared))
		return;
	memcpy(&SwitchPairing, shared, sizeof(SwitchPairing));
	SwitchProPairingAddressToLwbt(SwitchPairing.controller_bda,address);
	BD_ADDR(&SwitchAddress,address[0],address[1],address[2],address[3],
		address[4],address[5]);
	SwitchProIncomingImported(&SwitchIncoming);
	SetSwitchStatusFlag(SWITCH_PRO_STATUS_PAIRING_VALID);
}

static void BTSetControllerState(struct bte_pcb *sock, u32 State)
{
	u8 buf[2];
	buf[0] = 0x11;	//set LEDs and rumble
	buf[1] = State;
	bte_senddata(sock,buf,2);
}
static s32 BTHandleData(void *arg,void *buffer,u16 len)
{
	sync_before_read(arg, sizeof(struct BTPadStat));
	struct BTPadStat *stat = (struct BTPadStat*)arg;
	u32 chan = stat->channel;

	if(IsSwitchPad(stat))
	{
		s32 event = SwitchProIncomingHandleReport(&SwitchIncoming,
			(const u8*)buffer,len);
		u8 report_id = len > 0 ? ((const u8*)buffer)[0] : 0;
		SwitchStatus->command_reports = SwitchIncoming.command_reports;
		SwitchStatus->full_reports = SwitchIncoming.full_reports;
		SwitchStatus->init_sent = SwitchIncoming.init_sent;
		SwitchStatus->init_acks = SwitchIncoming.init_acks;
		SwitchStatus->init_retries = SwitchIncoming.init_retries;
		SwitchStatus->init_index = SwitchIncoming.init_index;
		if(report_id == SWITCH_PRO_REPORT_FULL)
			SwitchStatus->flags |= SWITCH_PRO_STATUS_FULL_SEEN;
		if(event == SWITCH_PRO_EVENT_ACK)
		{
			if(((const u8*)buffer)[14] == 0x02)
				SwitchStatus->flags |= SWITCH_PRO_STATUS_DEVICE_INFO;
			if(SwitchIncoming.init_complete)
				SwitchStatus->flags |= SWITCH_PRO_STATUS_INIT_COMPLETE;
			else
				BTSendSwitchInit(0);
		}
		else if(event == SWITCH_PRO_EVENT_INPUT)
		{
			SwitchStatus->parsed_reports++;
			SwitchStatus->flags |= SWITCH_PRO_STATUS_FULL_PARSED;
			PublishSwitchInput();
		}
		sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
		return ERR_OK;
	}

	if(*(u8*)buffer == 0x3D)	//21 expansion bytes report
	{
		if(stat->transferstate == TRANSFER_CALIBRATE)
		{
			stat->xAxisLmid = bswap16(R16((u32)(((u8*)buffer)+1)));
			stat->xAxisRmid = bswap16(R16((u32)(((u8*)buffer)+3)));
			stat->yAxisLmid = bswap16(R16((u32)(((u8*)buffer)+5)));
			stat->yAxisRmid = bswap16(R16((u32)(((u8*)buffer)+7)));
			stat->transferstate = TRANSFER_DONE;
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		if(chan == CHAN_NOT_SET)
			return ERR_OK;
		sync_before_read(&BTPad[chan], sizeof(struct BTPadCont));
		BTPad[chan].xAxisL = ((bswap16(R16((u32)(((u8*)buffer)+1))) - stat->xAxisLmid) *3) >>5;
		BTPad[chan].xAxisR = ((bswap16(R16((u32)(((u8*)buffer)+3))) - stat->xAxisRmid) *3) >>5;
		BTPad[chan].yAxisL = ((bswap16(R16((u32)(((u8*)buffer)+5))) - stat->yAxisLmid) *3) >>5;
		BTPad[chan].yAxisR = ((bswap16(R16((u32)(((u8*)buffer)+7))) - stat->yAxisRmid) *3) >>5;
		u32 prevButton = BTPad[chan].button;
		BTPad[chan].button = ~(R16((u32)(((u8*)buffer)+9)));
		if((!(prevButton & BT_BUTTON_SELECT)) && BTPad[chan].button & BT_BUTTON_SELECT)
		{
			//dbgprintf("Using %s control scheme\n", (stat->controller & C_SWAP) ? "orginal" : "swapped");
			stat->controller = (stat->controller & C_SWAP) ? (stat->controller & ~C_SWAP) : (stat->controller | C_SWAP);
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		BTPad[chan].used = stat->controller;
		sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
	}
	else if(*(u8*)buffer == 0x34)	//core buttons with 19 exptension bytes report
	{
		if(stat->transferstate == TRANSFER_CALIBRATE)
		{
			stat->xAxisLmid = *((u8*)buffer+3)&0x3F;
			stat->yAxisLmid = *((u8*)buffer+4)&0x3F;
			stat->xAxisRmid = ((*((u8*)buffer+5)&0x80)>>7) | ((*((u8*)buffer+4)&0xC0)>>5) | ((*((u8*)buffer+3)&0xC0)>>3);
			stat->yAxisRmid = *((u8*)buffer+5)&0x1F;
			stat->transferstate = TRANSFER_DONE;
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		if(chan == CHAN_NOT_SET || stat->controller == C_NOT_SET)
			return ERR_OK;
		sync_before_read(&BTPad[chan], sizeof(struct BTPadCont));
		
		BTPad[chan].xAxisL = ((*((u8*)buffer+3)&0x3F) - stat->xAxisLmid) <<2;
		BTPad[chan].xAxisR = ((((*((u8*)buffer+5)&0x80)>>7) | ((*((u8*)buffer+4)&0xC0)>>5) | ((*((u8*)buffer+3)&0xC0)>>3)) - stat->xAxisRmid) <<3;
		BTPad[chan].yAxisL = ((*((u8*)buffer+4)&0x3F) - stat->yAxisLmid) <<2;
		BTPad[chan].yAxisR = ((*((u8*)buffer+5)&0x1F) - stat->yAxisRmid) <<3;
		if(stat->controller & C_CC)
		{
			/* Calculate left trigger with deadzone */
			u8 tmp_triggerL = (((*((u8*)buffer+6)&0xE0)>>5) | ((*((u8*)buffer+5)&0x60)>>2))<<3;
			if(tmp_triggerL > DEADZONE)
				BTPad[chan].triggerL = (tmp_triggerL - DEADZONE) * 1.11f;
			else
				BTPad[chan].triggerL = 0;
			/* Calculate right trigger with deadzone */
			u8 tmp_triggerR = (*((u8*)buffer+6)&0x1F)<<3;
			if(tmp_triggerR > DEADZONE)
				BTPad[chan].triggerR = (tmp_triggerR - DEADZONE) * 1.11f;
			else
				BTPad[chan].triggerR = 0;
		}

		u32 prevButton = BTPad[chan].button;
		BTPad[chan].button = ~(R16((u32)(((u8*)buffer)+7))) | (*((u8*)buffer+2) & 0x10)<<4; //unused 0x100 for wiimote button Minus
		if((!(prevButton & BT_BUTTON_SELECT)) && BTPad[chan].button & BT_BUTTON_SELECT)
		{
			//dbgprintf("Using %s control scheme\n", (stat->controller & C_SWAP) ? "orginal" : "swapped");
			stat->controller = (stat->controller & C_SWAP) ? (stat->controller & ~C_SWAP) : (stat->controller | C_SWAP);
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		if((!(prevButton & (WM_BUTTON_MINUS << 4))) && BTPad[chan].button & (WM_BUTTON_MINUS << 4))	//wiimote button minus pressed leading edge
		{
			//dbgprintf("%s rumble for wiimote\n", (stat->controller & C_RUMBLE_WM) ? "Disabling" : "Enabling");
			stat->controller = (stat->controller & C_RUMBLE_WM) ? (stat->controller & ~C_RUMBLE_WM) : (stat->controller | C_RUMBLE_WM);
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		BTPad[chan].used = stat->controller;
		sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
	}
	else if(*(u8*)buffer == 0x37)	//Core Buttons and Accelerometer with 10 IR bytes and 6 Extension Bytes report
	{
		if(stat->transferstate == TRANSFER_CALIBRATE)
		{
			stat->xAxisLmid = *(((u8*)buffer)+16);
			stat->yAxisLmid = *(((u8*)buffer)+17);
//			stat->xAxisRmid = 0;
//			stat->yAxisRmid = 0;
			stat->transferstate = TRANSFER_DONE;
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}

		if(chan == CHAN_NOT_SET || stat->controller == C_NOT_SET)
			return ERR_OK;
		sync_before_read(&BTPad[chan], sizeof(struct BTPadCont));

		BTPad[chan].xAxisL = (*(((u8*)buffer)+16) - stat->xAxisLmid);
		BTPad[chan].yAxisL = (*(((u8*)buffer)+17) - stat->yAxisLmid);
		BTPad[chan].xAccel = (*(((u8*)buffer)+18) << 2) | ((*(((u8*)buffer)+21) & 0x0C) >> 2);
		BTPad[chan].yAccel = (*(((u8*)buffer)+19) << 2) | ((*(((u8*)buffer)+21) & 0x30) >> 4);
		BTPad[chan].zAccel = (*(((u8*)buffer)+20) << 2) | ((*(((u8*)buffer)+21) & 0xC0) >> 6);
		
		struct IRdot {
			bool	has_data;
			s32		x;
			s32		y;
		}IRdots[4];
		struct IRdot temp_dot = {0,0,0};

		IRdots[0].x = (u32)(*(((u8*)buffer)+6 )) | (((u32)(*(((u8*)buffer)+8 ) & 0x30)) << 4);
		IRdots[0].y = (u32)(*(((u8*)buffer)+7 )) | (((u32)(*(((u8*)buffer)+8 ) & 0xC0)) << 2);
		IRdots[1].x = (u32)(*(((u8*)buffer)+9 )) | (((u32)(*(((u8*)buffer)+8 ) & 0x03)) << 8);
		IRdots[1].y = (u32)(*(((u8*)buffer)+10)) | (((u32)(*(((u8*)buffer)+8 ) & 0x0C)) << 6);
		IRdots[2].x = (u32)(*(((u8*)buffer)+11)) | (((u32)(*(((u8*)buffer)+13) & 0x30)) << 4);
		IRdots[2].y = (u32)(*(((u8*)buffer)+12)) | (((u32)(*(((u8*)buffer)+13) & 0xC0)) << 2);
		IRdots[3].x = (u32)(*(((u8*)buffer)+14)) | (((u32)(*(((u8*)buffer)+13) & 0x03)) << 8);
		IRdots[3].y = (u32)(*(((u8*)buffer)+15)) | (((u32)(*(((u8*)buffer)+13) & 0x0C)) << 6);
		int dot;
		int num_dots = 0;
		for (dot = 0; dot < 4; dot++)
		{
			IRdots[dot].has_data = (IRdots[dot].x != 1023) && (IRdots[dot].y != 1023);
			if (IRdots[dot].has_data)
			{
				num_dots ++;
				temp_dot.x += IRdots[dot].x;
				temp_dot.y += IRdots[dot].y;
			}
		}
		if (num_dots == 2)
		{
			s32 SensorBarOffset = (*SensorBarPosition)? 128 : -128;

			temp_dot.x =  (512 - (temp_dot.x / num_dots)) / 4;	//origanally 0-1024
			BTPad[chan].xAxisR =  temp_dot.x;

			BTPad[chan].yAxisR = ((384 - (temp_dot.y / num_dots)) * 2 / 3) + SensorBarOffset;	//origonally 0-768
		}
		else
		{
			//use previous value. Currently does this automaticly but if someone decides to clear memory. 
		}

		u32 prevButton = BTPad[chan].button;
		BTPad[chan].button = ((R16((u32)((u8*)buffer+1))) & 0x1F9F) | ((~(*(((u8*)buffer)+21))&0x03)<<5);
		if((prevButton & WM_BUTTON_TWO) && BTPad[chan].button & WM_BUTTON_TWO)	//wiimote button TWO held down
		{
			switch (BTPad[chan].button & ~WM_BUTTON_TWO)
			{
				case WM_BUTTON_LEFT:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 1);
					break;
				case WM_BUTTON_RIGHT:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 2);
					break;
				case WM_BUTTON_UP:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 3);
					break;
				case WM_BUTTON_DOWN:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 4);
					break;
				case WM_BUTTON_MINUS:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 5);
					break;
				case WM_BUTTON_ONE:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 6);
					break;
				case WM_BUTTON_PLUS:
					stat->controller = (stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3)) | (C_NSWAP1 * 7);
					break;
				case NUN_BUTTON_C:
					if(!(prevButton & NUN_BUTTON_C))
						stat->controller = (stat->controller & C_SWAP) ? (stat->controller & ~C_SWAP) : (stat->controller | C_SWAP);
					break;
				case NUN_BUTTON_Z:
					if(!(prevButton & NUN_BUTTON_Z))
						stat->controller = (stat->controller & C_ISWAP) ? (stat->controller & ~C_ISWAP) : (stat->controller | C_ISWAP);
					break;
				case WM_BUTTON_A:
					if(!(prevButton & WM_BUTTON_A))
						stat->controller = (stat->controller & C_TestSWAP) ? (stat->controller & ~C_TestSWAP) : (stat->controller | C_TestSWAP);
					break;
				default: { }
			}
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}
		if((!(prevButton & WM_BUTTON_TWO)) && BTPad[chan].button & WM_BUTTON_TWO)	//wiimote button TWO pressed leading edge
		{
			stat->controller = stat->controller & ~(C_NSWAP1 | C_NSWAP2 | C_NSWAP3 | C_SWAP | C_ISWAP | C_TestSWAP);
			sync_after_write(arg, sizeof(struct BTPadStat));
			sync_before_read(arg, sizeof(struct BTPadStat));
		}

		BTPad[chan].used = stat->controller;
		sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
	}
	else if(*(u8*)buffer == 0x30)	//core buttons report
	{
		if(stat->transferstate == TRANSFER_CONNECT)
		{
			u8 buf[2];
			buf[0] = 0x15;	//request status report
			buf[1] = 0x00;	//turn off rumble
			bte_senddata(stat->sock,buf,2);	//returns 0x20 status report
			stat->transferstate = TRANSFER_EXT1;
			sync_after_write(arg, sizeof(struct BTPadStat));
		}
	}
	else if(*(u8*)buffer == 0x20)	//status report - responce to 0x15 or automaticly generated when expansion controller is plugged or unplugged
	{
		if(*((u8*)buffer+3) & 0x02)	//expansion controller connected
		{
			//Some third party wiimotes send the status report on their own while we are
			//still in TRANSFER_CONNECT, and never send another one in response to our
			//0x15 request. Accept it in either state so the handshake can proceed.
			if(stat->transferstate == TRANSFER_EXT1 || stat->transferstate == TRANSFER_CONNECT)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xA4; data[3] = 0x00; data[4] = 0xF0; //address
				data[5] = 0x01; //length
				data[6] = 0x55; //data deactivate motion plus
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result 
				stat->transferstate = TRANSFER_EXT2;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
		}
		else if(stat->transfertype == 0x34 || stat->transfertype == 0x37)
		{
			//reset
			stat->controller = C_NOT_SET;
			stat->timeout = read32(HW_TIMER);
			stat->transferstate = TRANSFER_EXT1;
			sync_after_write(arg, sizeof(struct BTPadStat));
			if(chan < CHAN_NOT_SET)
			{
				BTPad[chan].used = C_NOT_SET;
				sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
			}
		}
	}
	else if(*(u8*)buffer == 0x21)	//read memory data
	{
		if(stat->transferstate == TRANSFER_GET_IDENT)
		{
			const u32 ext_ctrl_id = R32((u32)((u8*)buffer+8));
			if((ext_ctrl_id == 0xA4200101) ||	//CLASSIC_CONTROLLER
			   (ext_ctrl_id == 0x90908f00) ||	//CLASSIC_CONTROLLER_NYKOWING
			   (ext_ctrl_id == 0x9e9f9c00) ||	//CLASSIC_CONTROLLER_NYKOWING2
			   (ext_ctrl_id == 0x908f8f00) ||	//CLASSIC_CONTROLLER_NYKOWING3
			   (ext_ctrl_id == 0xa5a2a300) ||	//CLASSIC_CONTROLLER_GENERIC
			   (ext_ctrl_id == 0x98999900) ||	//CLASSIC_CONTROLLER_GENERIC2
			   (ext_ctrl_id == 0xa0a1a000) ||	//CLASSIC_CONTROLLER_GENERIC3
			   (ext_ctrl_id == 0x8d8d8e00) ||	//CLASSIC_CONTROLLER_GENERIC4
			   (ext_ctrl_id == 0x93949400))		//CLASSIC_CONTROLLER_GENERIC5
			{
				if(*((u8*)buffer+6) == 0)
				{
					stat->controller = C_CC;
					//dbgprintf("Connected Classic Controller\n");
				}
				else
				{
					stat->controller = C_CCP;
					//dbgprintf("Connected Classic Controller Pro\n");
				}
				stat->transfertype = 0x34;
				/* Finally enable reading */
				u8 buf[3];
				buf[0] = 0x12;	//set data reporting mode
				buf[1] = 0x00;	//report only when data changes
				buf[2] = stat->transfertype;
				bte_senddata(stat->sock,buf,3);
				stat->transferstate = TRANSFER_CALIBRATE;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(ext_ctrl_id == 0xA4200000)
			{
				stat->controller = C_NUN;
				//dbgprintf("Connected NUNCHUCK\n");
				u8 data[2];
				data[0] = 0x13; //IR camera pixel clock
				data[1] = 0x06; //enable IR pixel clock and return 0x22
				bte_senddata(stat->sock,data,2);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_ENABLE_IR_PIXEL_CLOCK;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else
			{
				stat->transferstate = TRANSFER_CALIBRATE;	//todo was this for unknown controllers or just in the wrong spot?
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
		}
	}
	else if(*(u8*)buffer == 0x22)	//acknowledge output report, return function result 
	{
		if(*((u8*)buffer+3) & 0x02)	//??message being acknowledged todo lucky all needed messages had 2 bit set
		{
			if(stat->transferstate == TRANSFER_EXT2)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xA4; data[3] = 0x00; data[4] = 0xFB; //address
				data[5] = 0x01; //length
				data[6] = 0x00; //data unencrypt expansion bytes
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_SET_IDENT;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_SET_IDENT)
			{
				u8 data[7];
				data[0] = 0x17; //set mode to read
				data[1] = 0x04; //read from registers
				data[2] = 0xA4; data[3] = 0x00; data[4] = 0xFA; //address
				data[5] = 0x00; data[6] = 0x06; //length
				bte_senddata(stat->sock,data,7);	//returns 0x21 Read Memory Data 
				stat->transferstate = TRANSFER_GET_IDENT;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_ENABLE_IR_PIXEL_CLOCK)
			{
				u8 data[2];
				data[0] = 0x1a; //IR camera logic
				data[1] = 0x06; //enable IR logic and return 0x22
				bte_senddata(stat->sock,data,2);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_ENABLE_IR_LOGIC;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_ENABLE_IR_LOGIC)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xb0; data[3] = 0x00; data[4] = 0x30; //address
				data[5] = 0x01; //length
				data[6] = 0x01; //data
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_WRITE_IR_REG30_1;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_WRITE_IR_REG30_1)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xb0; data[3] = 0x00; data[4] = 0x00; //address
				data[5] = 0x09; //length
				switch (*IRSensitivity)
				{
					case 0:
						data[6] = 0x02; data[7] = 0x00; data[8] = 0x00; data[9] = 0x71; data[10] = 0x01; data[11] = 0x00; data[12] = 0x64; data[13] = 0x00; data[14] = 0xFE; //data
						break;
					case 1:
						data[6] = 0x02; data[7] = 0x00; data[8] = 0x00; data[9] = 0x71; data[10] = 0x01; data[11] = 0x00; data[12] = 0x96; data[13] = 0x00; data[14] = 0xB4; //data
						break;
					default:
					case 2:
						data[6] = 0x02; data[7] = 0x00; data[8] = 0x00; data[9] = 0x71; data[10] = 0x01; data[11] = 0x00; data[12] = 0xAA; data[13] = 0x00; data[14] = 0x64; //data
						break;
					case 3:
						data[6] = 0x02; data[7] = 0x00; data[8] = 0x00; data[9] = 0x71; data[10] = 0x01; data[11] = 0x00; data[12] = 0xC8; data[13] = 0x00; data[14] = 0x36; //data
						break;
					case 4:
						data[6] = 0x07; data[7] = 0x00; data[8] = 0x00; data[9] = 0x71; data[10] = 0x01; data[11] = 0x00; data[12] = 0x72; data[13] = 0x00; data[14] = 0x20; //data
						break;
				}
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_1;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_1)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xb0; data[3] = 0x00; data[4] = 0x1a; //address
				data[5] = 0x02; //length
				switch (*IRSensitivity)
				{
					case 0:
						data[6] = 0xFD; data[7] = 0x05; //data
						break;
					case 1:
						data[6] = 0xB3; data[7] = 0x04; //data
						break;
					default:
					case 2:
						data[6] = 0x63; data[7] = 0x03; //data
						break;
					case 3:
						data[6] = 0x35; data[7] = 0x03; //data
						break;
					case 4:
						data[6] = 0x1F; data[7] = 0x03; //data
						break;
				}
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_2;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_WRITE_IR_SENSITIVITY_BLOCK_2)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xb0; data[3] = 0x00; data[4] = 0x33; //address
				data[5] = 0x01; //length
				data[6] = 0x01; //data IR mode basic
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_WRITE_IR_MODE;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_WRITE_IR_MODE)
			{
				u8 data[22];
				memset(data, 0, 22);
				data[0] = 0x16; //set mode to write
				data[1] = 0x04; //write to registers
				data[2] = 0xb0; data[3] = 0x00; data[4] = 0x30; //address
				data[5] = 0x01; //length
				data[6] = 0x08; //data
				bte_senddata(stat->sock,data,22);	//returns 0x22 Acknowledge output report and return function result
				stat->transferstate = TRANSFER_WRITE_IR_REG30_8;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			else if(stat->transferstate == TRANSFER_WRITE_IR_REG30_8)
			{
				stat->transfertype = 0x37;
				/* Finally enable reading */
				u8 buf[3];
				buf[0] = 0x12;	//set data reporting mode
				buf[1] = 0x00;	//report only when data changes
				buf[2] = stat->transfertype;
				bte_senddata(stat->sock,buf,3);
				stat->transferstate = TRANSFER_CALIBRATE;
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
			//Third party wiimotes can lag their acks, so one may arrive while we are
			//waiting in EXT1 with no further status report coming. Ask again instead
			//of waiting forever.
			else if(stat->transferstate == TRANSFER_EXT1)
			{
				u8 buf[2];
				buf[0] = 0x15;	//request status report
				buf[1] = 0x00;
				bte_senddata(stat->sock,buf,2);
				sync_after_write(arg, sizeof(struct BTPadStat));
			}
		}
	//buffer[3] is the report id being acknowledged. Third party wiimotes also ack
		//the 0x11 player led/rumble report, and 0x11 has no 0x02 bit, so that ack used
		//to fall through to the reset below and throw the extension handshake back to
		//EXT1 for good. Never treat an led ack as an extension change.
		else if(*((u8*)buffer+3) != 0x11 && (stat->transfertype == 0x34 || stat->transfertype == 0x37))
		{
			//reset
			stat->controller = C_NOT_SET;
			stat->timeout = read32(HW_TIMER);
			stat->transferstate = TRANSFER_EXT1;
			sync_after_write(arg, sizeof(struct BTPadStat));
			if(chan < CHAN_NOT_SET)
			{
				BTPad[chan].used = C_NOT_SET;
				sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
			}
		}
		//fake wiiu pro controllers send 0x22 before accepting read commands
		if(stat->transferstate == TRANSFER_CALIBRATE && stat->transfertype == 0x3D)
		{
			u8 buf[3];
			buf[0] = 0x12;	//set data reporting mode
			buf[1] = 0x00;	//report only when data changes
			buf[2] = stat->transfertype;
			bte_senddata(stat->sock,buf,3);
			sync_after_write(arg, sizeof(struct BTPadStat));
		}
	}
	return ERR_OK;
}

static s32 BTHandleConnect(void *arg,struct bte_pcb *pcb,u8 err)
{
	sync_before_read(arg, sizeof(struct BTPadStat));
	struct BTPadStat *stat = (struct BTPadStat*)arg;

	if(IsSwitchPad(stat))
	{
		if(err != ERR_OK)
			return err;
		SwitchProIncomingChannels(&SwitchIncoming,
			pcb->ctl_pcb != NULL,pcb->data_pcb != NULL);
		return ERR_OK;
	}

	if(BTChannelsUsed >= 4)
	{
		bte_disconnect(stat->sock);
		return ERR_OK;
	}

	u8 buf[3];

	stat->channel = CHAN_NOT_SET;
	stat->rumble = 0;

	BTSetControllerState(stat->sock, LEDState[CHAN_NOT_SET]);

	//wiimote extensions need some extra stuff first, start with getting its status
	if(stat->transfertype == 0x34 || stat->transfertype == 0x37)
	{
		buf[0] = 0x12;	//set data reporting mode
		buf[1] = 0x00;	//report only when data changes
		buf[2] = 0x30; //get normal buttons once
		bte_senddata(stat->sock,buf,3);
		stat->transferstate = TRANSFER_CONNECT;
		stat->controller = C_NOT_SET;
		stat->timeout = read32(HW_TIMER);
	}
	else
	{
		//dbgprintf("Connected WiiU Pro Controller\n");
		buf[0] = 0x12;	//set data reporting mode
		buf[1] = 0x00;	//report only when data changes
		buf[2] = stat->transfertype;
		bte_senddata(stat->sock,buf,3);
		stat->transferstate = TRANSFER_CALIBRATE;
		stat->controller = C_CCP;
	}

	BTPadConnected[BTChannelsUsed] = stat;
	sync_after_write(stat, sizeof(struct BTPadStat));
	BTChannelsUsed++;
	return ERR_OK;
}

static s32 BTHandleDisconnect(void *arg,struct bte_pcb *pcb,u8 err)
{
	(void)pcb;
	(void)err;
	//dbgprintf("Controller disconnected\n");
	u32 i;
	for(i = 0; i < 4; ++i)
	{
		if(BTPadConnected[i] == arg)
		{
			if(BTChannelsUsed)
				BTChannelsUsed--;
			u32 chan = BTPadConnected[i]->channel;
			if(chan != CHAN_NOT_SET)
			{
				BTPad[chan].used = C_NOT_SET;
				sync_after_write(&BTPad[chan], 0x20);
			}
			while(i+1 < 4)
			{
				BTPadConnected[i] = BTPadConnected[i+1];
				BTPadConnected[i+1] = NULL;
				i++;
			}
			break;
		}
	}
	if(IsSwitchPad((struct BTPadStat*)arg))
	{
		SwitchProIncomingReset(&SwitchIncoming);
		SwitchProIncomingImported(&SwitchIncoming);
		SwitchProIncomingListener(&SwitchIncoming,ERR_OK);
		SwitchInitTimer = 0;
		SwitchTransportTimer = 0;
		SwitchPadStatus.controller = C_NOT_SET;
		SwitchPadStatus.channel = CHAN_NOT_SET;
		sync_after_write(&SwitchPadStatus, sizeof(SwitchPadStatus));
	}
	return ERR_OK;
}

static int RegisterBTPad(struct BTPadStat *stat, struct bd_addr *_bdaddr,
	u8 require_security)
{
	s32 result;
	stat->bdaddr = *_bdaddr;
	stat->sock = bte_new();

	if(stat->sock == NULL)
		return ERR_MEM;

	bte_arg(stat->sock, stat);
	bte_received(stat->sock, BTHandleData);
	bte_disconnected(stat->sock, BTHandleDisconnect);
	bte_require_security(stat->sock,require_security);

	result = bte_registerdeviceasync(stat->sock, _bdaddr, BTHandleConnect);
	sync_after_write(stat, sizeof(struct BTPadStat));

	return result;
}

static void EnsureSwitchPad(void)
{
	s32 result;
	if(!SwitchProIncomingNeedsListener(&SwitchIncoming))
		return;
	if(SwitchPadStatus.sock == NULL)
	{
		memset(&SwitchPadStatus, 0, sizeof(SwitchPadStatus));
		SwitchPadStatus.transfertype = SWITCH_PRO_REPORT_BASIC;
		SwitchPadStatus.channel = CHAN_NOT_SET;
		result = RegisterBTPad(&SwitchPadStatus,&SwitchAddress,1);
	}
	else
		result = bte_registerdeviceasync(SwitchPadStatus.sock,
			&SwitchAddress,BTHandleConnect);
	SwitchProIncomingListener(&SwitchIncoming,result);
	SwitchStatus->listener_result = result;
	SetSwitchStatusError(result);
	if(result == ERR_OK)
		SetSwitchStatusFlag(SWITCH_PRO_STATUS_LISTENER_READY);
}

static s32 BTCompleteCB(s32 result,void *usrdata)
{
	(void)usrdata;
	u32 i;
	struct bd_addr bdaddr;

	if(result == ERR_OK)
	{
		BTStackReady = 1;
		/* Reserve the imported controller's listeners first.  They use a
		 * dedicated slot and never depend on how many vWii pads are stored. */
		EnsureSwitchPad();
		for(i = 0; i <BTDevices->num_registered; i++)
		{
			BD_ADDR(&(bdaddr),BTDevices->registered[i].bdaddr[5],BTDevices->registered[i].bdaddr[4],BTDevices->registered[i].bdaddr[3],
							BTDevices->registered[i].bdaddr[2],BTDevices->registered[i].bdaddr[1],BTDevices->registered[i].bdaddr[0]);
			if(IsSwitchAddress(&bdaddr))
			{
				SwitchStatus->stored_address_matches++;
				SwitchStatus->flags |=
					SWITCH_PRO_STATUS_DUPLICATE_LISTENER;
			}

			if(strstr(BTDevices->registered[i].name, "-UC") != NULL)	//if wiiu pro controller
				BTPadStatus[i].transfertype = 0x3D;
			else
				BTPadStatus[i].transfertype = 0x34;
			BTPadStatus[i].channel = CHAN_NOT_SET;
			RegisterBTPad(&BTPadStatus[i],&(bdaddr),0);
		}
	}
	return ERR_OK;
}

static s32 BTPatchCB(s32 result,void *usrdata)
{
	(void)result;
	(void)usrdata;
	BTE_InitSub(BTCompleteCB);
	return ERR_OK;
}

static s32 BTInitCoreCB(s32 result, void *usrdata)
{
	(void)usrdata;
	if(result == ERR_OK)
		BTE_ApplyPatch(BTPatchCB);
	return ERR_OK;
}

u8 BTSwitchIncomingGetKey(struct bd_addr *bdaddr,u8 *key)
{
	if(!IsSwitchAddress(bdaddr) || key == NULL)
		return 0;
	SwitchStatus->key_requests++;
	SetSwitchStatusFlag(SWITCH_PRO_STATUS_KEY_REPLIED);
	SwitchProPairingCopyHciLinkKey(&SwitchPairing,key);
	return 1;
}

u8 BTSwitchIncomingACL(struct bd_addr *bdaddr)
{
	if(!IsSwitchAddress(bdaddr))
		return 0;
	SwitchProIncomingACL(&SwitchIncoming,ERR_OK);
	SwitchStatus->acl_count++;
	SetSwitchStatusFlag(SWITCH_PRO_STATUS_ACL_CONNECTED);
	/* The controller owns security setup on an incoming stored-key
	 * reconnect.  It requests the link key and enables encryption itself;
	 * issuing a second Authentication Requested here makes the otherwise
	 * valid reconnect end in HCI Authentication Failure (0x05). */
	return 1;
}

void BTSwitchIncomingAuthentication(struct bd_addr *bdaddr,u8 result)
{
	s32 command_result;
	if(!IsSwitchAddress(bdaddr))
		return;
	SwitchProIncomingAuthentication(&SwitchIncoming,result);
	SwitchStatus->auth_result = result;
	if(result == HCI_SUCCESS)
		SetSwitchStatusFlag(SWITCH_PRO_STATUS_AUTHENTICATED);
	else if(!SwitchIncoming.encrypted)
		SetSwitchStatusError(ERR_CONN);
	if(result != HCI_SUCCESS)
	{
		if(!SwitchIncoming.encrypted)
			bte_security_complete(SwitchPadStatus.sock,ERR_CONN);
		return;
	}
	if(SwitchIncoming.encrypted)
		return;
	command_result = hci_set_connection_encrypt(bdaddr,1);
	if(command_result == ERR_OK)
		SetSwitchStatusFlag(SWITCH_PRO_STATUS_ENCRYPT_REQUESTED);
	if(command_result != ERR_OK)
	{
		SetSwitchStatusError(command_result);
		bte_security_complete(SwitchPadStatus.sock,command_result);
	}
}

void BTSwitchIncomingEncryption(struct bd_addr *bdaddr,u8 result,u8 enabled)
{
	if(!IsSwitchAddress(bdaddr))
		return;
	SwitchProIncomingEncryption(&SwitchIncoming,result,enabled);
	SwitchStatus->encrypt_result = result | ((u32)enabled << 8);
	if(result == HCI_SUCCESS && enabled)
	{
		SetSwitchStatusFlag(SWITCH_PRO_STATUS_AUTHENTICATED);
		SetSwitchStatusFlag(SWITCH_PRO_STATUS_ENCRYPTED);
	}
	else
		SetSwitchStatusError(ERR_CONN);
	bte_security_complete(SwitchPadStatus.sock,
		(result == HCI_SUCCESS && enabled) ? ERR_OK : ERR_CONN);
	if(result == HCI_SUCCESS && enabled)
		SwitchStatus->flags |= SWITCH_PRO_STATUS_TRANSPORT_PENDING;
}

static u32 BTSwitchChannelOwner(struct bte_pcb *owner)
{
	u32 i;
	if(owner == NULL)
		return SWITCH_PRO_STATUS_OWNER_UNKNOWN;
	if(owner == SwitchPadStatus.sock)
		return SWITCH_PRO_STATUS_OWNER_DEDICATED;
	for(i = 0; i < CONF_PAD_MAX_REGISTERED; i++)
	{
		if(owner == BTPadStatus[i].sock)
			return SWITCH_PRO_STATUS_OWNER_REGULAR_BASE + i;
	}
	return SWITCH_PRO_STATUS_OWNER_UNKNOWN;
}

void BTSwitchIncomingChannel(struct bd_addr *bdaddr,u8 control_channel,
	struct bte_pcb *owner)
{
	u32 owner_code;
	if(!IsSwitchAddress(bdaddr))
		return;
	owner_code = BTSwitchChannelOwner(owner);
	if(control_channel)
	{
		SwitchStatus->control_count++;
		SwitchStatus->flags |= SWITCH_PRO_STATUS_CONTROL_OPEN;
		SwitchStatus->control_channel_owner = owner_code;
		if(owner_code == SWITCH_PRO_STATUS_OWNER_DEDICATED)
			SwitchStatus->flags |= SWITCH_PRO_STATUS_CONTROL_DEDICATED;
	}
	else
	{
		SwitchStatus->interrupt_count++;
		SwitchStatus->flags |= SWITCH_PRO_STATUS_INTERRUPT_OPEN;
		SwitchStatus->interrupt_channel_owner = owner_code;
		if(owner_code == SWITCH_PRO_STATUS_OWNER_DEDICATED)
			SwitchStatus->flags |= SWITCH_PRO_STATUS_INTERRUPT_DEDICATED;
	}
	SwitchProIncomingChannels(&SwitchIncoming,
		SwitchIncoming.control_open || control_channel,
		SwitchIncoming.interrupt_open || !control_channel);
	sync_after_write(SwitchStatus, sizeof(*SwitchStatus));
}

void BTSwitchIncomingCommandStatus(u8 command,u8 result)
{
	if(!SwitchIncoming.imported || result == HCI_SUCCESS)
		return;
	if(command != HCI_AUTHENTICATION_REQUESTED &&
		command != HCI_SET_CONN_ENCRYPT)
		return;
	if(SwitchIncoming.encrypted)
		return;
	SetSwitchStatusError(ERR_CONN);
	bte_security_complete(SwitchPadStatus.sock,ERR_CONN);
}

u32 BTTimer = 0;
u32 inited = 0;
void BTInit(void)
{
	BTStackReady = 0;
	ImportSwitchPairing();

	memset(BTPad, 0, sizeof(struct BTPadCont)*4);
	sync_after_write(BTPad, sizeof(struct BTPadCont)*4);

	/* Both Motor and Channel free */
	memset((void*)BTMotor, 0, 0x20);
	sync_after_write((void*)BTMotor, 0x20);

	BTE_Init();
	BTE_InitCore(BTInitCoreCB);

	BTTimer = read32(HW_TIMER);
	u32 CheckTimer = read32(HW_TIMER);
	inited = 1;
	while(TimerDiffSeconds(CheckTimer) < 1)
	{
		BTUpdateRegisters();
		udelay(200);
	}
}

void BTUpdateRegisters(void)
{
	if(inited == 0)
		return;
	if(BTStackReady && SwitchProIncomingNeedsListener(&SwitchIncoming))
		EnsureSwitchPad();
	BTPumpSwitchConnection();
	if(SwitchIncoming.finalized && SwitchIncoming.awaiting_ack &&
		!SwitchIncoming.init_complete && !SwitchIncoming.init_failed &&
		SwitchInitTimer != 0 &&
		TimerDiffTicks(SwitchInitTimer) >= SWITCH_PRO_INIT_RETRY_TICKS)
		BTSendSwitchInit(1);

	if(intr == 1)
	{
		intr = 0;
		__readintrdataCB();
		__issue_intrread();
	}
	if(bulk == 1)
	{
		bulk = 0;
		__readbulkdataCB();
		__issue_bulkread();
	}

	u32 i = 0, j = 0;
	sync_before_read((void*)0x13003020,0x40);
	for( ; i < BTChannelsUsed; ++i)
	{
		sync_before_read(BTPadConnected[i], sizeof(struct BTPadStat));
		u32 LastChan = BTPadConnected[i]->channel;
		u32 LastRumble = BTPadConnected[i]->rumble;
		u32 CurChan = CHAN_NOT_SET;
		u32 CurRumble = 0;
		if(BTPadConnected[i]->controller != C_NOT_SET)
		{
			for( ; j < 4; ++j)
			{
				if(BTPadFree[j] == 1)
				{
					CurChan = j;
					CurRumble = BTMotor[j];
					j++;
					break;
				}
			}
		}
		else if(TimerDiffSeconds(BTPadConnected[i]->timeout) >= 20)
		{
			bte_disconnect(BTPadConnected[i]->sock);
			break;
		}
		if(CurRumble == 0)
		{
			if(LastRumble == 1)
			{
				if(TimerDiffTicks(BTPadConnected[i]->rumbletime) > 94922)
					BTPadConnected[i]->rumbletime = 0;
				else //extend to at least 1/20th of a second
					CurRumble = 1;
			}
		}
		else if(CurRumble == 1)
		{
			if(LastRumble != 1)
				BTPadConnected[i]->rumbletime = read32(HW_TIMER);
		}
		else if(CurRumble == 2) //direct stop
			CurRumble = 0;

		if(LastChan != CurChan || LastRumble != CurRumble)
		{
			if(CurChan == CHAN_NOT_SET || ((LastChan != CHAN_NOT_SET) && CurChan < LastChan))
			{
				BTPad[LastChan].used = C_NOT_SET;
				sync_after_write(&BTPad[LastChan], sizeof(struct BTPadCont));
			}
			BTPadConnected[i]->channel = CurChan;
			BTPadConnected[i]->rumble = CurRumble;
			if(IsSwitchPad(BTPadConnected[i]))
				PublishSwitchInput();
			else if(BTPadConnected[i]->transfertype == 0x3D || BTPadConnected[i]->controller & (C_RUMBLE_WM | C_NUN) || ConfigGetConfig(NIN_CFG_CC_RUMBLE))
				BTSetControllerState(BTPadConnected[i]->sock, LEDState[CurChan] | CurRumble);
			else //classic controller doesnt have rumble, can be forced to wiimote if wanted
				BTSetControllerState(BTPadConnected[i]->sock, LEDState[CurChan]);
			sync_after_write(BTPadConnected[i], sizeof(struct BTPadStat));
		}
	}
	if(TimerDiffSeconds(BTTimer) > 0)
	{
		//dbgprintf("tick\n");
		l2cap_tmr(); //every second
		BTTimer = read32(HW_TIMER);
	}
}
