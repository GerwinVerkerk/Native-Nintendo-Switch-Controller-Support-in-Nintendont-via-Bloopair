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
#include "SwitchPro.h"
#include "ff_utf8.h"
#include "../common/include/SwitchProDiag.h"
#include "../common/include/SwitchProTrace.h"
#include "../common/include/SwitchProPairing.h"

extern int dbgprintf( const char *fmt, ...);

static vu32 BTChannelsUsed = 0;
extern vu32 intr, bulk;

static conf_pads *BTDevices = (conf_pads*)0x132C0000;
static struct BTPadStat *BTPadConnected[4];

static struct BTPadStat BTPadStatus[CONF_PAD_MAX_REGISTERED] ALIGNED(32);
static struct linkkey_info BTKeys[CONF_PAD_MAX_REGISTERED] ALIGNED(32);
static u32 BTKeyCount = 0;
static SwitchProPairing *SwitchPairing = (SwitchProPairing*)SWITCH_PRO_PAIRING_ADDR;
static volatile u32 BTDiagnosticStage = 0;
static struct bd_addr BTDiagnosticTarget;
static u8 BTDiagnosticTargetSet = 0;
static u8 BTDiagnosticStorePending = 0;
static u8 BTDiagnosticLinkKey[16];
static u8 BTDiagnosticLinkKeyValid = 0;
static u8 BTDiagnosticAuthRequested = 0;
static u8 BTDiagnosticAuthenticated = 0;
static u8 BTDiagnosticEncrypted = 0;
static struct bd_addr BTAuthenticationCommandTarget;
static u8 BTAuthenticationCommandPending = 0;
static struct bd_addr BTEncryptionCommandTarget;
static u8 BTEncryptionCommandPending = 0;
static u8 BTDiagnosticBlinkOn = 1;
static u32 BTDiagnosticBlinkTimer = 0;
static volatile u32 BTDiagnosticSwitchButtons = 0;
static volatile u32 BTDiagnosticSwitchChannel = 4;

static struct BTPadCont *BTPad = (struct BTPadCont*)0x132F0000;
static struct SwitchProArmDiag *SwitchArmDiag =
	(struct SwitchProArmDiag*)SWITCH_PRO_DIAG_ARM_ADDR;
static struct SwitchProPpcDiag *SwitchPpcDiag =
	(struct SwitchProPpcDiag*)SWITCH_PRO_DIAG_PPC_ADDR;
static struct SwitchProTraceBuffer *SwitchArmTrace =
	(struct SwitchProTraceBuffer*)SWITCH_PRO_TRACE_ARM_ADDR;
static struct SwitchProTraceBuffer *SwitchPpcTrace =
	(struct SwitchProTraceBuffer*)SWITCH_PRO_TRACE_PPC_ADDR;
static struct SwitchProInput SwitchTraceLastInput;
static u8 SwitchTraceLastInputValid = 0;
static u32 SwitchTraceCaptureTimer = 0;
static u32 SwitchTraceDumpRetryTimer = 0;
static u8 SwitchTraceCaptureStarted = 0;
static u8 SwitchTraceDumpComplete = 0;
static u8 SwitchTraceDumpAttempts = 0;

#define SWITCH_TRACE_AUTO_DUMP_SECONDS 120
#define SWITCH_TRACE_DUMP_RETRY_SECONDS 5
#define SWITCH_TRACE_DUMP_MAX_ATTEMPTS 3

static vu32* BTMotor = (u32*)0x13003040;
static vu32* BTPadFree = (u32*)0x13003050;

static vu32* IRSensitivity = (u32*)0x132C0490;
static vu32* SensorBarPosition = (u32*)0x132C0494;

static const u8 LEDState[] = { 0x10, 0x20, 0x40, 0x80, 0xF0 };

#define CHAN_NOT_SET 4

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
#define C_SWITCH_PRO	(1<<10)

#define TRANSFER_SWITCH_PRO 0xF0
#define SWITCH_DIAG_HID_OPEN        (1<<0)
#define SWITCH_DIAG_ENCRYPTED       (1<<1)
#define SWITCH_DIAG_PROTOCOL_STARTED (1<<2)
#define SWITCH_PRO_TIMER_TICKS_PER_MS 1898
#define SWITCH_INITIAL_PAGE_WINDOW_SECONDS 30
#define SWITCH_INQUIRY_PAGE_WINDOW_SECONDS 10
#define SWITCH_INQUIRY_MAX_ATTEMPTS 6
static const s8 DEADZONE = 0x1A;

static u32 SwitchInquiryRetryTimer;
static u8 SwitchInquiryActive;
static u8 SwitchInquiryAttempts;
static u8 SwitchInquiryTargetFound;
static u8 BTPadRegistrationInitialized;
static u8 BTPadRegisteredCount;

static void BTSwitchStartProtocol(struct BTPadStat *stat);
static s32 BTHandleConnect(void *arg,struct bte_pcb *pcb,u8 err);
static void BTDiagnosticSetTarget(const struct bd_addr *bdaddr);
static void BTRegisterPersistentPads(void);

static void BTSwitchTraceArm(u32 type, u32 a, u32 b, u32 c, u32 d, u32 e,
	u32 f, u32 g, u32 h, u32 i)
{
	u32 index = SwitchArmTrace->count;
	struct SwitchProTraceEvent *event;
	if(index >= SWITCH_PRO_TRACE_EVENTS)
	{
		SwitchArmTrace->dropped++;
		return;
	}
	event = &SwitchArmTrace->events[index];
	event->sequence = index + 1;
	event->ticks = read32(HW_TIMER);
	event->type = type;
	event->data[0] = a;
	event->data[1] = b;
	event->data[2] = c;
	event->data[3] = d;
	event->data[4] = e;
	event->data[5] = f;
	event->data[6] = g;
	event->data[7] = h;
	event->data[8] = i;
	SwitchArmTrace->count = index + 1;
}

static u32 BTSwitchTraceWord(const u8 *buffer, u16 len, u16 offset)
{
	u32 value = 0;
	u16 copy = len > offset ? len - offset : 0;
	if(copy > 4)
		copy = 4;
	if(copy)
		memcpy(&value, buffer + offset, copy);
	return value;
}

static struct BTPadStat *BTFindSwitchStat(const struct bd_addr *bdaddr)
{
	u32 i;
	for(i = 0; i < CONF_PAD_MAX_REGISTERED; i++)
	{
		if(BTPadStatus[i].transfertype == TRANSFER_SWITCH_PRO &&
			memcmp(BTPadStatus[i].bdaddr.addr, bdaddr->addr,
				sizeof(bdaddr->addr)) == 0)
			return &BTPadStatus[i];
	}
	return NULL;
}

static void BTFailSwitchSecurity(struct BTPadStat *stat)
{
	if(stat == NULL)
		return;
	SwitchProTransportFail(&stat->switch_transport);
	if(stat->sock != NULL)
		bte_security_complete(stat->sock, ERR_CONN);
}

static err_t BTRequestSwitchAuthentication(struct BTPadStat *stat)
{
	err_t result;
	if(stat == NULL)
		return ERR_VAL;
	result = hci_authentication_requested(&stat->bdaddr);
	if(result == ERR_OK)
	{
		BTAuthenticationCommandTarget = stat->bdaddr;
		BTAuthenticationCommandPending = 1;
	}
	else
	{
		BTDiagnosticHIDHostEvent(&stat->bdaddr,
			BT_HID_HOST_PAIRING_RETRY, result, 1);
		BTFailSwitchSecurity(stat);
	}
	return result;
}

static err_t BTRequestSwitchEncryption(struct BTPadStat *stat)
{
	err_t result;
	if(stat == NULL)
		return ERR_VAL;
	result = hci_set_connection_encrypt(&stat->bdaddr, 1);
	if(result == ERR_OK)
	{
		BTEncryptionCommandTarget = stat->bdaddr;
		BTEncryptionCommandPending = 1;
	}
	else
	{
		BTDiagnosticHIDHostEvent(&stat->bdaddr,
			BT_HID_HOST_TRANSPORT_READY, result, 1);
		BTFailSwitchSecurity(stat);
	}
	return result;
}

static void BTCompleteSwitchSecurity(struct BTPadStat *stat)
{
	u8 action;
	if(stat == NULL)
		return;
	if(!stat->switch_remote_name_requested)
	{
		stat->switch_remote_name_requested = 1;
		hci_read_remote_name(&stat->bdaddr);
	}
	action = SwitchProTransportSecurityReady(&stat->switch_transport);
	stat->diagnostic_state |= SWITCH_DIAG_ENCRYPTED;
	BTDiagnosticHIDHostEvent(&stat->bdaddr, BT_HID_HOST_TRANSPORT_READY,
		action, stat->switch_transport.origin);
	bte_security_complete(stat->sock, ERR_OK);
	sync_after_write(stat, sizeof(struct BTPadStat));
}

static int RegisterBTPad(struct BTPadStat *stat, struct bd_addr *_bdaddr);

static struct BTPadStat *BTFindRegisteredStat(const struct bd_addr *bdaddr)
{
	u32 i;
	for(i = 0; i < BTPadRegisteredCount; i++)
	{
		if(memcmp(BTPadStatus[i].bdaddr.addr, bdaddr->addr,
			sizeof(bdaddr->addr)) == 0)
			return &BTPadStatus[i];
	}
	return NULL;
}

static struct BTPadStat *BTPrepareSwitchSlot(const struct bd_addr *bdaddr,
	u8 origin)
{
	struct BTPadStat *stat;
	u32 slot;
	BTRegisterPersistentPads();
	stat = BTFindRegisteredStat(bdaddr);
	if(stat == NULL)
	{
		if(BTPadRegisteredCount >= CONF_PAD_MAX_REGISTERED)
			return NULL;
		slot = BTPadRegisteredCount++;
		stat = &BTPadStatus[slot];
		memset(stat, 0, sizeof(*stat));
		stat->channel = CHAN_NOT_SET;
		stat->switch_led_channel = CHAN_NOT_SET;
	}
	stat->transfertype = TRANSFER_SWITCH_PRO;
	stat->bdaddr = *bdaddr;
	SwitchProTransportBegin(&stat->switch_transport, origin);
	stat->switch_link_key_store_pending = 0;
	stat->switch_remote_name_requested = 0;
	stat->switch_remote_name_verified = 0;
	if(origin == SWITCH_PRO_CONNECTION_OUTGOING)
	{
		/* SYNC/discovery denotes a fresh bond.  A cached vWii key for the
		 * same address may belong to an older host pairing and must not be
		 * offered before SSP has a chance to create a replacement key. */
		stat->switch_force_new_pairing = 1;
		stat->switch_pairing_retry_done = 0;
		stat->switch_link_key_valid = 0;
	}
	BTDiagnosticSetTarget(bdaddr);
	if(stat->sock == NULL)
		RegisterBTPad(stat, (struct bd_addr*)bdaddr);
	else if(origin == SWITCH_PRO_CONNECTION_INCOMING)
	{
		bte_require_security(stat->sock, 1);
		bte_registerdeviceasync(stat->sock, (struct bd_addr*)bdaddr,
			BTHandleConnect);
	}
	else
	{
		bte_require_security(stat->sock, 1);
		bte_registerhidhostasync(stat->sock, (struct bd_addr*)bdaddr,
			BTHandleConnect);
	}
	return stat;
}

static void BTDiagnosticAdvanceSecurity(void)
{
	if(BTDiagnosticStage >= BT_DIAG_HID_OPEN && BTDiagnosticAuthenticated &&
		BTDiagnosticStage < BT_DIAG_AUTHENTICATED)
		BTDiagnosticStage = BT_DIAG_AUTHENTICATED;
	if(BTDiagnosticStage >= BT_DIAG_AUTHENTICATED && BTDiagnosticEncrypted &&
		BTDiagnosticStage < BT_DIAG_ENCRYPTED)
		BTDiagnosticStage = BT_DIAG_ENCRYPTED;
}

static void BTDiagnosticSetTarget(const struct bd_addr *bdaddr)
{
	SwitchInquiryTargetFound = 1;
	if(BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0)
		return;
	BTDiagnosticTarget = *bdaddr;
	BTDiagnosticTargetSet = 1;
	BTDiagnosticStage = BT_DIAG_FOUND;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_PHASE, BT_DIAG_FOUND,
		0, 0, 0, 0, 0, 0, 0, 0);
	BTDiagnosticAuthenticated = 0;
	BTDiagnosticEncrypted = 0;
	BTDiagnosticLinkKeyValid = 0;
	BTDiagnosticAuthRequested = 0;
	BTDiagnosticBlinkOn = 1;
	BTDiagnosticBlinkTimer = read32(HW_TIMER);
}

static void BTRegisterPersistentPads(void)
{
	struct bd_addr bdaddr;
	u32 i, count;
	if(BTPadRegistrationInitialized)
		return;
	count = BTDevices->num_registered;
	if(count >= CONF_PAD_MAX_REGISTERED)
		count = CONF_PAD_MAX_REGISTERED - 1;
	BTPadRegistrationInitialized = 1;
	for(i = 0; i < count; i++)
	{
		BD_ADDR(&bdaddr, BTDevices->registered[i].bdaddr[5],
			BTDevices->registered[i].bdaddr[4],
			BTDevices->registered[i].bdaddr[3],
			BTDevices->registered[i].bdaddr[2],
			BTDevices->registered[i].bdaddr[1],
			BTDevices->registered[i].bdaddr[0]);
		if(strstr(BTDevices->registered[i].name, "Pro Controller") != NULL &&
			strstr(BTDevices->registered[i].name, "-UC") == NULL)
		{
			BTPadStatus[i].transfertype = TRANSFER_SWITCH_PRO;
			SwitchProTransportBegin(&BTPadStatus[i].switch_transport,
				SWITCH_PRO_CONNECTION_INCOMING);
			BTDiagnosticSetTarget(&bdaddr);
		}
		else if(strstr(BTDevices->registered[i].name, "-UC") != NULL)
			BTPadStatus[i].transfertype = 0x3D;
		else
			BTPadStatus[i].transfertype = 0x34;
		BTPadStatus[i].channel = CHAN_NOT_SET;
		RegisterBTPad(&BTPadStatus[i], &bdaddr);
		BTPadRegisteredCount = i + 1;
	}
}

u8 BTDiagnosticConnectionTarget(const struct bd_addr *bdaddr)
{
	struct BTPadStat *stat;
	if(bdaddr == NULL)
		return 0;
	stat = BTFindSwitchStat(bdaddr);
	if(stat != NULL)
	{
		BTDiagnosticSetTarget(bdaddr);
		SwitchProTransportACLReady(&stat->switch_transport);
		/* A HID host initiates authentication on the newly established ACL.
		 * A missing key will drive SSP; a stored key will be requested normally. */
		BTRequestSwitchAuthentication(stat);
		return 1;
	}
	return 0;
}

void BTDiagnosticIncomingConnectionRequest(const struct bd_addr *bdaddr,
	const u8 *cod)
{
	if(bdaddr == NULL || cod == NULL)
		return;
	BTDiagnosticHIDHostEvent(bdaddr, BT_HID_HOST_INCOMING_REQUEST,
		((u32)cod[0] << 16) | ((u32)cod[1] << 8) | cod[2], 0);
	if(cod[0] != 0x08 || cod[1] != 0x25 || cod[2] != 0x00)
		return;
	{
		struct BTPadStat *stat = BTPrepareSwitchSlot(bdaddr,
			SWITCH_PRO_CONNECTION_INCOMING);
		if(stat != NULL)
			BTDiagnosticHIDHostEvent(bdaddr,
				BT_HID_HOST_SLOT_CREATED, SWITCH_PRO_CONNECTION_INCOMING,
				stat - BTPadStatus);
	}
}

void BTDiagnosticACLResult(const struct bd_addr *bdaddr, u32 result)
{
	struct BTPadStat *stat = bdaddr != NULL ? BTFindSwitchStat(bdaddr) : NULL;
	u32 target = bdaddr != NULL && BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_HID_HOST, BT_HID_HOST_ACL_REQUEST,
		result, target, 0, 0, 0, 0, 0, 0);
	if(stat != NULL && stat->sock != NULL)
	{
		stat->sock->acl_connect_pending = 0;
		stat->sock->acl_connected = result == HCI_SUCCESS;
		if(result != HCI_SUCCESS)
		{
			SwitchProTransportFail(&stat->switch_transport);
			stat->sock->security_ready = 0;
			stat->sock->hid_connect_started = 0;
		}
	}
}

void BTDiagnosticHIDHostEvent(const struct bd_addr *bdaddr, u32 stage,
	u32 result, u32 status)
{
	u32 target = bdaddr != NULL && BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_HID_HOST, stage, result, status,
		target, BTDiagnosticAuthenticated, BTDiagnosticEncrypted,
		0, 0, 0);
}

void BTDiagnosticPairingPhase(u32 phase, const struct bd_addr *bdaddr)
{
	if(!BTDiagnosticTargetSet || bdaddr == NULL ||
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr, sizeof(BTDiagnosticTarget.addr)) != 0)
		return;
	/* Keep the display cumulative: a later stage only proves its predecessor. */
	if(phase == BTDiagnosticStage + 1)
	{
		BTDiagnosticStage = phase;
		BTSwitchTraceArm(SWITCH_TRACE_ARM_PHASE, phase, 0, 0, 0, 0,
			0, 0, 0, 0);
	}
}

void BTDiagnosticLinkKeyQueued(const struct bd_addr *bdaddr)
{
	struct BTPadStat *stat;
	if(bdaddr == NULL)
		return;
	stat = BTFindSwitchStat(bdaddr);
	if(stat == NULL)
		return;
	BTDiagnosticStorePending = 1;
	stat->switch_link_key_store_pending = 1;
}

u8 BTDiagnosticCacheLinkKey(const struct bd_addr *bdaddr, const u8 *key)
{
	struct BTPadStat *stat;
	if(bdaddr == NULL || key == NULL)
		return 0;
	stat = BTFindSwitchStat(bdaddr);
	if(stat != NULL)
	{
		memcpy(stat->switch_link_key, key, sizeof(stat->switch_link_key));
		stat->switch_link_key_valid = 1;
		stat->switch_force_new_pairing = 0;
		/* Persist only after authentication and encryption complete.  Sending
		 * Write Stored Link Key from Link Key Notification consumes the sole
		 * HCI command credit while authentication is still in progress. */
		stat->switch_link_key_store_pending = 1;
	}
	if(BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0)
	{
		memcpy(BTDiagnosticLinkKey, key, sizeof(BTDiagnosticLinkKey));
		BTDiagnosticLinkKeyValid = 1;
	}
	return stat != NULL;
}

u8 BTDiagnosticGetLinkKey(const struct bd_addr *bdaddr, u8 *key)
{
	u32 i;
	struct BTPadStat *stat;
	if(bdaddr == NULL || key == NULL)
		return 0;
	stat = BTFindSwitchStat(bdaddr);
	if(stat != NULL && stat->switch_force_new_pairing)
		return 0;
	if(stat != NULL && stat->switch_link_key_valid)
	{
		memcpy(key, stat->switch_link_key, sizeof(stat->switch_link_key));
		return 1;
	}
	if(BTDiagnosticTargetSet && BTDiagnosticLinkKeyValid &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0)
	{
		memcpy(key, BTDiagnosticLinkKey, sizeof(BTDiagnosticLinkKey));
		return 1;
	}
	for(i = 0; i < BTKeyCount; i++)
	{
		if(memcmp(BTKeys[i].bdaddr.addr, bdaddr->addr,
			sizeof(bdaddr->addr)) == 0)
		{
			memcpy(key, BTKeys[i].key, sizeof(BTKeys[i].key));
			return 1;
		}
	}
	return 0;
}

void BTDiagnosticLinkKeyStoreResult(u8 result)
{
	u32 i;
	if(!BTDiagnosticStorePending)
		return;
	BTDiagnosticStorePending = 0;
	for(i = 0; i < BTPadRegisteredCount; i++)
	{
		if(BTPadStatus[i].switch_link_key_store_pending == 2)
		{
			BTPadStatus[i].switch_link_key_store_pending = 0;
			if(result == HCI_SUCCESS)
			{
				if(BTDiagnosticStage == BT_DIAG_SSP_COMPLETE)
					BTDiagnosticStage = BT_DIAG_LINK_KEY_STORED;
				BTCompleteSwitchSecurity(&BTPadStatus[i]);
			}
			else
				BTFailSwitchSecurity(&BTPadStatus[i]);
		}
	}
}

void BTDiagnosticAuthenticationCommandResult(u8 result)
{
	struct BTPadStat *stat;
	if(!BTAuthenticationCommandPending)
		return;
	BTAuthenticationCommandPending = 0;
	stat = BTFindSwitchStat(&BTAuthenticationCommandTarget);
	if(result == HCI_SUCCESS)
		BTDiagnosticAuthRequested = 1;
	else
	{
		BTDiagnosticStage = BT_DIAG_AUTH_FAILED;
		BTFailSwitchSecurity(stat);
	}
}

void BTDiagnosticEncryptionCommandResult(u8 result)
{
	struct BTPadStat *stat;
	if(!BTEncryptionCommandPending)
		return;
	BTEncryptionCommandPending = 0;
	if(result == HCI_SUCCESS)
		return;
	stat = BTFindSwitchStat(&BTEncryptionCommandTarget);
	BTDiagnosticStage = BT_DIAG_ENCRYPT_FAILED;
	BTFailSwitchSecurity(stat);
}

void BTDiagnosticAuthenticationResult(u8 result, const struct bd_addr *bdaddr)
{
	struct BTPadStat *stat = bdaddr != NULL ? BTFindSwitchStat(bdaddr) : NULL;
	u8 target = bdaddr != NULL && BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_PHASE, BT_DIAG_AUTHENTICATED,
		result, target, 0, 0, 0, 0, 0, 0);
	if(stat == NULL)
		return;
	if(SwitchProAuthenticationShouldRetry(result,
		stat->switch_pairing_retry_done))
	{
		/* The remote rejected a stale host key.  Retry authentication once;
		 * BTDiagnosticGetLinkKey() will now send a negative reply, which
		 * starts the SSP sequence and replaces only this controller's key. */
		stat->switch_pairing_retry_done = 1;
		stat->switch_force_new_pairing = 1;
		stat->switch_link_key_valid = 0;
		if(target)
		{
			BTDiagnosticLinkKeyValid = 0;
			BTDiagnosticHIDHostEvent(bdaddr,
				BT_HID_HOST_PAIRING_RETRY, result, 0);
		}
		BTRequestSwitchAuthentication(stat);
		return;
	}
	if(result != HCI_SUCCESS)
	{
		if(target)
			BTDiagnosticStage = BT_DIAG_AUTH_FAILED;
		BTFailSwitchSecurity(stat);
		return;
	}
	stat->switch_transport.authenticated = 1;
	if(target)
	{
		BTDiagnosticAuthenticated = 1;
		BTDiagnosticAdvanceSecurity();
	}
	BTRequestSwitchEncryption(stat);
}

void BTDiagnosticEncryptionResult(u8 result, u8 enabled,
	const struct bd_addr *bdaddr)
{
	struct BTPadStat *stat = bdaddr != NULL ? BTFindSwitchStat(bdaddr) : NULL;
	u8 target = bdaddr != NULL && BTDiagnosticTargetSet &&
		memcmp(BTDiagnosticTarget.addr, bdaddr->addr,
			sizeof(BTDiagnosticTarget.addr)) == 0;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_PHASE, BT_DIAG_ENCRYPTED,
		result, enabled, target, 0, 0, 0, 0, 0);
	if(stat == NULL)
		return;
	if(result != HCI_SUCCESS || !enabled)
	{
		if(target)
			BTDiagnosticStage = BT_DIAG_ENCRYPT_FAILED;
		BTFailSwitchSecurity(stat);
		return;
	}
	if(target)
	{
		BTDiagnosticEncrypted = 1;
		BTDiagnosticAdvanceSecurity();
	}
	stat->switch_transport.encrypted = 1;
	if(stat->switch_link_key_store_pending == 1)
	{
		err_t store_result = hci_write_stored_link_key(&stat->bdaddr,
			stat->switch_link_key);
		if(store_result != ERR_OK)
		{
			stat->switch_link_key_store_pending = 0;
			BTFailSwitchSecurity(stat);
			return;
		}
		stat->switch_link_key_store_pending = 2;
		BTDiagnosticStorePending = 1;
		return;
	}
	BTCompleteSwitchSecurity(stat);
}

void BTDiagnosticHIDChannelsOpen(const struct bd_addr *bdaddr)
{
	BTDiagnosticPairingPhase(BT_DIAG_HID_OPEN, bdaddr);
	BTDiagnosticAdvanceSecurity();
}

void BTDiagnosticHIDChannelOpen(const struct bd_addr *bdaddr,
	u8 control_channel)
{
	struct BTPadStat *stat = bdaddr != NULL ? BTFindSwitchStat(bdaddr) : NULL;
	u8 action;
	if(stat == NULL)
		return;
	action = SwitchProTransportChannelReady(&stat->switch_transport,
		control_channel);
	BTDiagnosticHIDHostEvent(bdaddr,
		control_channel ? BT_HID_HOST_CONTROL_COMPLETE :
			BT_HID_HOST_INTERRUPT_COMPLETE,
		0, action);
}

void BTDiagnosticRemoteNameResult(const struct bd_addr *bdaddr, u8 result,
	const u8 *name, u16 length)
{
	struct BTPadStat *stat = bdaddr != NULL ? BTFindSwitchStat(bdaddr) : NULL;
	u16 name_len = 0;
	u8 verified = 0;
	if(name != NULL)
	{
		while(name_len < length && name[name_len] != 0)
			name_len++;
		if(name_len == 14 && memcmp(name, "Pro Controller", 14) == 0)
			verified = 1;
	}
	if(stat != NULL && result == HCI_SUCCESS)
	{
		stat->switch_remote_name_verified = verified;
		if(verified)
			stat->switch_transport.identity_confirmed = 1;
		else
		{
			SwitchProTransportFail(&stat->switch_transport);
			hci_disconnect(&stat->bdaddr,
				HCI_OTHER_END_TERMINATED_CONN_USER_ENDED);
		}
	}
	BTDiagnosticHIDHostEvent(bdaddr, BT_HID_HOST_REMOTE_NAME,
		result, ((u32)verified << 16) | name_len);
}

static s32 BTSwitchSendSubcommand(struct BTPadStat *stat, u8 command,
	const u8 *data, u8 data_len)
{
	u8 report[16];
	u16 len = SwitchProBuildSubcommand(&stat->switch_state, report,
		sizeof(report), command, data, data_len);
	BTSwitchTraceArm(SWITCH_TRACE_ARM_TX_SUBCOMMAND, command, data_len,
		len, stat->switch_state.init_state, stat->switch_state.init_retries,
		BTSwitchTraceWord(report, len, 0),
		BTSwitchTraceWord(report, len, 4),
		BTSwitchTraceWord(report, len, 8),
		BTSwitchTraceWord(report, len, 12));
	if(len)
		return bte_senddata(stat->sock, report, len);
	return ERR_VAL;
}

static void BTSwitchProtocolReady(struct BTPadStat *stat)
{
	BTDiagnosticPairingPhase(BT_DIAG_PROTOCOL_READY, &stat->bdaddr);
	SwitchProInitStart(&stat->switch_state);
	stat->switch_init_timer = read32(HW_TIMER);
	sync_after_write(stat, sizeof(struct BTPadStat));
}

static void BTSwitchStartProtocol(struct BTPadStat *stat)
{
	if((stat->diagnostic_state & (SWITCH_DIAG_HID_OPEN |
		SWITCH_DIAG_ENCRYPTED | SWITCH_DIAG_PROTOCOL_STARTED)) !=
		(SWITCH_DIAG_HID_OPEN | SWITCH_DIAG_ENCRYPTED))
		return;
	stat->diagnostic_state |= SWITCH_DIAG_PROTOCOL_STARTED;
	/* A Switch Pro Controller already uses report protocol on its incoming
	 * HID channels.  The working Linux A-wake trace sends no HID SET_PROTOCOL
	 * transaction: it starts subcommand 0x02 directly on interrupt PSM 0x13.
	 * Waiting for a SET_PROTOCOL handshake here can stall initialization. */
	BTSwitchProtocolReady(stat);
}

static void BTSwitchUpdateProtocol(struct BTPadStat *stat)
{
	u16 delay_ms = SwitchProInitDelayMs(&stat->switch_state);
	u8 action;
	u8 data[5];
	/* All four LEDs identify the provisional, not-yet-assigned state.  The
	 * main loop replaces this with exactly one LED only after PADReadGC has
	 * exposed a real free GameCube channel. */
	u8 led = 0x0F;

	if(delay_ms == 0 || TimerDiffTicks(stat->switch_init_timer) <
		(u32)delay_ms * SWITCH_PRO_TIMER_TICKS_PER_MS)
		return;

	action = SwitchProInitPoll(&stat->switch_state);
	stat->switch_init_timer = read32(HW_TIMER);
	if(action == SWITCH_PRO_INIT_ACTION_DEVICE_INFO)
		BTSwitchSendSubcommand(stat, SWITCH_PRO_SUBCMD_DEVICE_INFO, NULL, 0);
	else if(action == SWITCH_PRO_INIT_ACTION_PLAYER_LED)
		BTSwitchSendSubcommand(stat, SWITCH_PRO_SUBCMD_PLAYER_LED, &led, 1);
	else if(action == SWITCH_PRO_INIT_ACTION_VIBRATION)
	{
		data[0] = 1;
		BTSwitchSendSubcommand(stat, SWITCH_PRO_SUBCMD_VIBRATION, data, 1);
	}
	else if(action == SWITCH_PRO_INIT_ACTION_USER_CAL ||
		action == SWITCH_PRO_INIT_ACTION_FACTORY_CAL)
	{
		u32 address = action == SWITCH_PRO_INIT_ACTION_USER_CAL ?
			SWITCH_PRO_USER_CAL_ADDR : SWITCH_PRO_FACTORY_CAL_ADDR;
		data[0] = address & 0xFF;
		data[1] = (address >> 8) & 0xFF;
		data[2] = (address >> 16) & 0xFF;
		data[3] = (address >> 24) & 0xFF;
		data[4] = action == SWITCH_PRO_INIT_ACTION_USER_CAL ? 22 : 18;
		BTSwitchSendSubcommand(stat, SWITCH_PRO_SUBCMD_SPI_READ, data, 5);
	}
	else if(action == SWITCH_PRO_INIT_ACTION_REPORT_MODE)
	{
		data[0] = SWITCH_PRO_REPORT_FULL;
		BTSwitchSendSubcommand(stat, SWITCH_PRO_SUBCMD_REPORT_MODE, data, 1);
	}
	sync_after_write(stat, sizeof(struct BTPadStat));
}

static void BTSwitchPublishInput(struct BTPadStat *stat)
{
	u32 chan = stat->channel;
	u32 buttons;
	if(!stat->switch_input_valid || chan == CHAN_NOT_SET ||
		stat->controller == C_NOT_SET)
		return;
	buttons = stat->switch_selftest_state == 1 ? SWITCH_PRO_BTN_A :
		stat->switch_input.buttons;

	sync_before_read(&BTPad[chan], sizeof(struct BTPadCont));
	BTPad[chan].xAxisL = stat->switch_input.left_x;
	BTPad[chan].yAxisL = stat->switch_input.left_y;
	BTPad[chan].xAxisR = stat->switch_input.right_x;
	BTPad[chan].yAxisR = stat->switch_input.right_y;
	BTPad[chan].button = buttons;
	BTPad[chan].triggerL = 0;
	BTPad[chan].triggerR = 0;
	/* The Switch-specific type remains private to the ARM state machine.
	 * PPC consumes the already-supported Classic Controller Pro contract. */
	BTPad[chan].used = C_CCP;
	sync_after_write(&BTPad[chan], sizeof(struct BTPadCont));
	BTSwitchTraceArm(SWITCH_TRACE_ARM_PUBLISH, chan, C_CCP, buttons,
		((u16)stat->switch_input.left_x << 16) |
			(u16)stat->switch_input.left_y,
		((u16)stat->switch_input.right_x << 16) |
			(u16)stat->switch_input.right_y,
		BTSwitchTraceWord((const u8*)&BTPad[chan], sizeof(struct BTPadCont), 0),
		BTSwitchTraceWord((const u8*)&BTPad[chan], sizeof(struct BTPadCont), 4),
		BTSwitchTraceWord((const u8*)&BTPad[chan], sizeof(struct BTPadCont), 8),
		BTSwitchTraceWord((const u8*)&BTPad[chan], sizeof(struct BTPadCont), 12));
	BTDiagnosticSwitchButtons = buttons;
	BTDiagnosticSwitchChannel = chan;
	if(BTDiagnosticStage < BT_DIAG_INPUT_RECEIVED)
		BTDiagnosticPairingPhase(BT_DIAG_INPUT_RECEIVED, &stat->bdaddr);
	stat->switch_publish_count++;
	SwitchArmDiag->publish_sequence++;
	SwitchArmDiag->publish_channel = chan;
	SwitchArmDiag->publish_used = C_CCP;
	SwitchArmDiag->publish_buttons = buttons;
	SwitchArmDiag->publish_left_x = stat->switch_input.left_x;
	SwitchArmDiag->publish_left_y = stat->switch_input.left_y;
	SwitchArmDiag->publish_right_x = stat->switch_input.right_x;
	SwitchArmDiag->publish_right_y = stat->switch_input.right_y;
	SwitchArmDiag->selftest_state = stat->switch_selftest_state;
	sync_after_write(SwitchArmDiag, sizeof(struct SwitchProArmDiag));
}

static s32 BTHandleSwitchProData(struct BTPadStat *stat, void *buffer, u16 len)
{
	struct SwitchProInput input;
	u32 chan = stat->channel;
	u8 report_id = len ? ((u8*)buffer)[0] : 0;
	u8 parsed = SwitchProParseReport(&stat->switch_state,
		(const u8*)buffer, len, &input);
	u8 stream_ready = SwitchProTrackStreamReport(&stat->switch_state,
		report_id, parsed);
	if(report_id == SWITCH_PRO_REPORT_COMMAND ||
		(parsed && (!SwitchTraceLastInputValid ||
		memcmp(&SwitchTraceLastInput, &input, sizeof(input)) != 0)))
	{
		BTSwitchTraceArm(SWITCH_TRACE_ARM_RX_REPORT, report_id, len, parsed,
			stream_ready, BTSwitchTraceWord(buffer, len, 0),
			BTSwitchTraceWord(buffer, len, 4),
			BTSwitchTraceWord(buffer, len, 8),
			BTSwitchTraceWord(buffer, len, 12),
			BTSwitchTraceWord(buffer, len, 16));
		if(parsed)
		{
			SwitchTraceLastInput = input;
			SwitchTraceLastInputValid = 1;
			BTSwitchTraceArm(SWITCH_TRACE_ARM_PARSE, input.buttons,
				((u16)input.left_x << 16) | (u16)input.left_y,
				((u16)input.right_x << 16) | (u16)input.right_y,
				report_id, stat->switch_input_reports, 0, 0, 0, 0);
		}
	}
	{
		u32 sequence = ++SwitchArmDiag->report_sequence;
		struct SwitchProDiagReport *entry =
			&SwitchArmDiag->reports[(sequence - 1) % SWITCH_PRO_DIAG_REPORTS];
		u32 copy_len = len < sizeof(entry->raw) ? len : sizeof(entry->raw);
		memset(entry, 0, sizeof(*entry));
		entry->sequence = sequence;
		entry->report_id_len = ((u32)report_id << 16) | len;
		if(parsed)
		{
			entry->buttons = input.buttons;
			entry->left_x = input.left_x;
			entry->left_y = input.left_y;
			entry->right_x = input.right_x;
			entry->right_y = input.right_y;
		}
		memcpy(entry->raw, buffer, copy_len);
		sync_after_write(SwitchArmDiag, sizeof(struct SwitchProArmDiag));
	}

	if(parsed && (report_id == SWITCH_PRO_REPORT_FULL ||
		report_id == SWITCH_PRO_REPORT_BASIC))
	{
		stat->switch_input = input;
		stat->switch_input_valid = 1;
		stat->switch_input_reports++;
		if(stream_ready && stat->switch_transport.identity_confirmed &&
			!(stat->controller & C_SWITCH_PRO))
		{
			stat->controller = C_CCP | C_SWITCH_PRO;
			sync_after_write(stat, sizeof(struct BTPadStat));
		}
		if(stream_ready && stat->switch_transport.identity_confirmed &&
			chan != CHAN_NOT_SET)
			BTSwitchPublishInput(stat);
	}

	/* Command responses place the acknowledgement and subcommand at 13/14.
	 * The receive path only advances state; the periodic update sends the
	 * next command after the required pacing interval. */
	if(len >= 15 && report_id == SWITCH_PRO_REPORT_COMMAND)
	{
		u8 ack = ((u8*)buffer)[13];
		u8 command = ((u8*)buffer)[14];
		const u8 *response_data = len > 15 ? &((u8*)buffer)[15] : NULL;
		u16 response_len = len > 15 ? len - 15 : 0;
		u8 ack_result = SwitchProInitHandleResponse(&stat->switch_state,
			ack, command, response_data, response_len);
		if(ack_result == SWITCH_PRO_ACK_ACCEPTED &&
			command == SWITCH_PRO_SUBCMD_DEVICE_INFO)
			stat->switch_transport.identity_confirmed = 1;
		if(ack_result == SWITCH_PRO_ACK_ACCEPTED ||
			ack_result == SWITCH_PRO_ACK_NEGATIVE)
			stat->switch_init_timer = read32(HW_TIMER);
	}

	/* Preserve parser state and the subcommand report counter across callbacks. */
	sync_after_write(stat, sizeof(struct BTPadStat));
	return ERR_OK;
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

	if(stat->transfertype == TRANSFER_SWITCH_PRO)
		return BTHandleSwitchProData(stat, buffer, len);

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
	if(err != ERR_OK)
	{
		if(stat->transfertype == TRANSFER_SWITCH_PRO)
			SwitchProTransportFail(&stat->switch_transport);
		sync_after_write(stat, sizeof(struct BTPadStat));
		return err;
	}

	if(BTChannelsUsed >= 4)
	{
		bte_disconnect(stat->sock);
		return ERR_OK;
	}

	u8 buf[3];

	stat->channel = CHAN_NOT_SET;
	stat->rumble = 0;

	if(stat->transfertype != TRANSFER_SWITCH_PRO)
		BTSetControllerState(stat->sock, LEDState[CHAN_NOT_SET]);

	//wiimote extensions need some extra stuff first, start with getting its status
	if(stat->transfertype == TRANSFER_SWITCH_PRO)
	{
		SwitchProReset(&stat->switch_state);
		stat->switch_init_timer = 0;
		stat->switch_input_valid = 0;
		stat->switch_input_reports = 0;
		stat->switch_publish_count = 0;
		stat->switch_led_channel = CHAN_NOT_SET;
		stat->transferstate = TRANSFER_DONE;
		/* Only claim a player slot after a valid Switch input report arrives. */
		stat->controller = C_NOT_SET;
		stat->timeout = read32(HW_TIMER);
		stat->diagnostic_state |= SWITCH_DIAG_HID_OPEN;
		if(stat->switch_transport.encrypted)
			stat->diagnostic_state |= SWITCH_DIAG_ENCRYPTED;
		BTSwitchStartProtocol(stat);
	}
	else if(stat->transfertype == 0x34 || stat->transfertype == 0x37)
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
	//dbgprintf("Controller disconnected\n");
	if(BTChannelsUsed) BTChannelsUsed--;
	u32 i;
	for(i = 0; i < 4; ++i)
	{
		if(BTPadConnected[i] == arg)
		{
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
	if(((struct BTPadStat*)arg)->transfertype == TRANSFER_SWITCH_PRO)
	{
		struct BTPadStat *stat = (struct BTPadStat*)arg;
		stat->channel = CHAN_NOT_SET;
		stat->controller = C_NOT_SET;
		stat->diagnostic_state = 0;
		SwitchProReset(&stat->switch_state);
		stat->switch_input_valid = 0;
		stat->switch_input_reports = 0;
		stat->switch_publish_count = 0;
		stat->switch_led_channel = CHAN_NOT_SET;
		SwitchProTransportBegin(&stat->switch_transport,
			SWITCH_PRO_CONNECTION_INCOMING);
		sync_after_write(stat, sizeof(struct BTPadStat));
		bte_registerdeviceasync(stat->sock, &stat->bdaddr, BTHandleConnect);
	}
	return ERR_OK;
}

static int RegisterBTPad(struct BTPadStat *stat, struct bd_addr *_bdaddr)
{
	stat->bdaddr = *_bdaddr;
	stat->diagnostic_state = 0;
	if(stat->sock == NULL)
		stat->sock = bte_new();

	if(stat->sock == NULL)
		return ERR_OK;

	bte_arg(stat->sock, stat);
	bte_received(stat->sock, BTHandleData);
	bte_disconnected(stat->sock, BTHandleDisconnect);
	bte_require_security(stat->sock,
		stat->transfertype == TRANSFER_SWITCH_PRO ? 1 : 0);

	if(stat->transfertype == TRANSFER_SWITCH_PRO &&
		stat->switch_transport.origin == SWITCH_PRO_CONNECTION_OUTGOING)
		bte_registerhidhostasync(stat->sock, _bdaddr, BTHandleConnect);
	else if(stat->transfertype == TRANSFER_SWITCH_PRO)
		bte_registerdeviceasync(stat->sock, _bdaddr, BTHandleConnect);
	else
		bte_registerdeviceasync(stat->sock, _bdaddr, BTHandleConnect);
	sync_after_write(stat, sizeof(struct BTPadStat));

	return ERR_OK;
}

static s32 BTPairInquiryCB(s32 result,void *usrdata)
{
	struct inquiry_info_ex info[CONF_PAD_MAX_REGISTERED];
	s32 found = 0;
	u32 i, count, cod;
	u8 switch_found = 0;

	SwitchInquiryActive = 0;
	SwitchInquiryRetryTimer = read32(HW_TIMER);

	if(result == ERR_OK)
		found = BTE_GetInquiryResults(info, CONF_PAD_MAX_REGISTERED);
	for(i = 0; i < (u32)found; i++)
	{
		if(info[i].cod[0] == 0x08 && info[i].cod[1] == 0x25 &&
			info[i].cod[2] == 0x00)
		{
			switch_found = 1;
			break;
		}
	}
	if(switch_found)
		SwitchInquiryTargetFound = 1;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_INQUIRY, result, found, BTKeyCount,
		BTDevices->num_registered, switch_found, 0, 0, 0, 0);
	for(i = 0; i < (u32)found && i < 4; i++)
	{
		cod = ((u32)info[i].cod[0] << 16) |
			((u32)info[i].cod[1] << 8) | info[i].cod[2];
		BTSwitchTraceArm(SWITCH_TRACE_ARM_INQUIRY, i, cod,
			cod == 0x082500, 0, 0, 0, 0, 0, 0);
	}

	BTRegisterPersistentPads();
	count = BTPadRegisteredCount;

	/* Nintendo Switch Pro Controller class of device: 0x002508. */
	for(i = 0; i < (u32)found; i++)
	{
		struct BTPadStat *known;
		u8 slot_action;
		if(info[i].cod[0] != 0x08 || info[i].cod[1] != 0x25 || info[i].cod[2] != 0x00)
			continue;
		known = BTFindRegisteredStat(&info[i].bdaddr);
		slot_action = SwitchProSlotAction(known != NULL,
			known != NULL && known->transfertype == TRANSFER_SWITCH_PRO);
		if(known != NULL)
		{
			if(slot_action == SWITCH_PRO_SLOT_PROMOTE)
			{
				u32 slot = known - BTPadStatus;
				BTDiagnosticHIDHostEvent(&info[i].bdaddr,
					BT_HID_HOST_SLOT_PROMOTED, known->transfertype, slot);
				known = BTPrepareSwitchSlot(&info[i].bdaddr,
					SWITCH_PRO_CONNECTION_OUTGOING);
			}
			else if(slot_action == SWITCH_PRO_SLOT_REUSE)
			{
				BTDiagnosticSetTarget(&info[i].bdaddr);
				BTDiagnosticHIDHostEvent(&info[i].bdaddr,
					BT_HID_HOST_SLOT_REUSED, 0,
					known->sock != NULL);
				if(known->sock != NULL && !known->sock->conn_notified &&
					!known->sock->hid_connect_started &&
					!known->sock->acl_connect_pending &&
					!known->sock->acl_connected)
				{
					/* A controller in SYNC/discovery mode requests a fresh
					 * bond.  Do not invalidate a live connection merely
					 * because it also appeared in inquiry results. */
					known->switch_force_new_pairing = 1;
					known->switch_pairing_retry_done = 0;
					known->switch_link_key_valid = 0;
					known->switch_link_key_store_pending = 0;
					known->switch_remote_name_requested = 0;
					known->switch_remote_name_verified = 0;
					SwitchProTransportBegin(&known->switch_transport,
						SWITCH_PRO_CONNECTION_OUTGOING);
					BTDiagnosticHIDHostEvent(&info[i].bdaddr,
						BT_HID_HOST_ACL_RETRY, 0, 0);
					bte_registerhidhostasync(known->sock,
						&info[i].bdaddr, BTHandleConnect);
				}
			}
			break;
		}
		if(count >= CONF_PAD_MAX_REGISTERED)
			continue;
		BTDiagnosticSetTarget(&info[i].bdaddr);
		BTPadStatus[count].transfertype = TRANSFER_SWITCH_PRO;
		BTPadStatus[count].channel = CHAN_NOT_SET;
		BTPadStatus[count].switch_force_new_pairing = 1;
		BTPadStatus[count].switch_pairing_retry_done = 0;
		BTPadStatus[count].switch_link_key_valid = 0;
		BTPadStatus[count].switch_link_key_store_pending = 0;
		BTPadStatus[count].switch_remote_name_requested = 0;
		BTPadStatus[count].switch_remote_name_verified = 0;
		SwitchProTransportBegin(&BTPadStatus[count].switch_transport,
			SWITCH_PRO_CONNECTION_OUTGOING);
		RegisterBTPad(&BTPadStatus[count], &info[i].bdaddr);
		BTDiagnosticHIDHostEvent(&info[i].bdaddr,
			BT_HID_HOST_SLOT_CREATED, 0, count);
		count++;
		BTPadRegisteredCount = count;
		break;
	}
	return ERR_OK;
}

static s32 BTCompleteCB(s32 result,void *usrdata)
{
	if(result == ERR_OK)
	{
		BTRegisterPersistentPads();
		/* Inquiry and page scan share the same Classic Bluetooth radio.  A
		 * continuous series of inquiries starves the incoming A-wake route:
		 * the controller pages the host while the host is busy in inquiry.
		 * Keep a page-scan-only window first, then alternate bounded inquiry
		 * rounds with long page-scan windows. */
		SwitchInquiryRetryTimer = read32(HW_TIMER);
		SwitchInquiryActive = 0;
		SwitchInquiryAttempts = 0;
	}
	return ERR_OK;
}

static s32 BTPatchCB(s32 result,void *usrdata)
{
	BTE_InitSub(BTCompleteCB);
	return ERR_OK;
}

static s32 BTReadLinkKeyCB(s32 result,void *usrdata)
{
	u32 i;
	BTKeyCount = result > 0 ? (u32)result : 0;
	if(BTKeyCount > CONF_PAD_MAX_REGISTERED)
		BTKeyCount = CONF_PAD_MAX_REGISTERED;
	sync_before_read(SwitchPairing, sizeof(*SwitchPairing));
	if(SwitchProPairingIsValid(SwitchPairing))
	{
		for(i = 0; i < BTKeyCount; i++)
		{
			if(memcmp(BTKeys[i].bdaddr.addr, SwitchPairing->controller_bda,
				sizeof(BTKeys[i].bdaddr.addr)) == 0)
				break;
		}
		if(i < CONF_PAD_MAX_REGISTERED)
		{
			memcpy(BTKeys[i].bdaddr.addr, SwitchPairing->controller_bda,
				sizeof(BTKeys[i].bdaddr.addr));
			memcpy(BTKeys[i].key, SwitchPairing->link_key,
				sizeof(BTKeys[i].key));
			if(i == BTKeyCount)
				BTKeyCount++;
		}
	}
	BTE_ApplyPatch(BTPatchCB);
	return ERR_OK;
}

static s32 BTInitCoreCB(s32 result, void *usrdata)
{
	if(result == ERR_OK)
		BTE_ReadStoredLinkKey(BTKeys, CONF_PAD_MAX_REGISTERED, BTReadLinkKeyCB);
	return ERR_OK;
}

u32 BTTimer = 0;
u32 inited = 0;
void BTInit(void)
{
	SwitchInquiryRetryTimer = 0;
	SwitchInquiryActive = 0;
	SwitchInquiryAttempts = 0;
	SwitchInquiryTargetFound = 0;
	BTPadRegistrationInitialized = 0;
	BTPadRegisteredCount = 0;
	BTDiagnosticStage = 0;
	BTDiagnosticTargetSet = 0;
	BTDiagnosticStorePending = 0;
	BTDiagnosticLinkKeyValid = 0;
	BTDiagnosticAuthRequested = 0;
	BTDiagnosticAuthenticated = 0;
	BTDiagnosticEncrypted = 0;
	BTAuthenticationCommandPending = 0;
	BTEncryptionCommandPending = 0;
	BTDiagnosticBlinkOn = 1;
	BTDiagnosticBlinkTimer = read32(HW_TIMER);
	BTDiagnosticSwitchButtons = 0;
	BTDiagnosticSwitchChannel = CHAN_NOT_SET;
	memset(SwitchArmDiag, 0, sizeof(struct SwitchProArmDiag));
	SwitchArmDiag->magic = SWITCH_PRO_DIAG_MAGIC;
	SwitchArmDiag->version = SWITCH_PRO_DIAG_VERSION;
	sync_after_write(SwitchArmDiag, sizeof(struct SwitchProArmDiag));
	memset(SwitchPpcDiag, 0, sizeof(struct SwitchProPpcDiag));
	sync_after_write(SwitchPpcDiag, sizeof(struct SwitchProPpcDiag));
	memset(SwitchArmTrace, 0, SWITCH_PRO_TRACE_REGION_SIZE);
	SwitchArmTrace->magic = SWITCH_PRO_TRACE_MAGIC;
	SwitchArmTrace->version = SWITCH_PRO_TRACE_VERSION;
	SwitchArmTrace->writer = SWITCH_TRACE_WRITER_ARM;
	BTSwitchTraceArm(SWITCH_TRACE_ARM_INIT, sizeof(struct BTPadCont),
		sizeof(struct SwitchProTraceBuffer), SWITCH_PRO_TRACE_REGION_SIZE,
		0, 0, 0, 0, 0, 0);
	sync_after_write(SwitchArmTrace, SWITCH_PRO_TRACE_REGION_SIZE);
	memset(SwitchPpcTrace, 0, SWITCH_PRO_TRACE_REGION_SIZE);
	SwitchPpcTrace->magic = SWITCH_PRO_TRACE_MAGIC;
	SwitchPpcTrace->version = SWITCH_PRO_TRACE_VERSION;
	SwitchPpcTrace->writer = SWITCH_TRACE_WRITER_PPC;
	sync_after_write(SwitchPpcTrace, SWITCH_PRO_TRACE_REGION_SIZE);
	memset(&SwitchTraceLastInput, 0, sizeof(SwitchTraceLastInput));
	SwitchTraceLastInputValid = 0;
	SwitchTraceCaptureTimer = read32(HW_TIMER);
	SwitchTraceDumpRetryTimer = 0;
	SwitchTraceCaptureStarted = 1;
	SwitchTraceDumpComplete = 0;
	SwitchTraceDumpAttempts = 0;
	memset(BTKeys, 0, sizeof(struct linkkey_info) * CONF_PAD_MAX_REGISTERED);

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

static u8 BTSwitchTraceWriteFile(void)
{
	FIL trace;
	u32 wrote;
	if(SwitchArmTrace->magic != SWITCH_PRO_TRACE_MAGIC ||
		SwitchArmTrace->count == 0)
		return 0;
	sync_before_read(SwitchPpcTrace, SWITCH_PRO_TRACE_REGION_SIZE);
	if(f_open_char(&trace, "/switch-pro-trace.bin",
		FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	wrote = 0;
	if(f_write(&trace, SwitchArmTrace, SWITCH_PRO_TRACE_REGION_SIZE,
		&wrote) != FR_OK || wrote != SWITCH_PRO_TRACE_REGION_SIZE)
	{
		f_close(&trace);
		return 0;
	}
	wrote = 0;
	if(f_write(&trace, SwitchPpcTrace, SWITCH_PRO_TRACE_REGION_SIZE,
		&wrote) != FR_OK || wrote != SWITCH_PRO_TRACE_REGION_SIZE)
	{
		f_close(&trace);
		return 0;
	}
	f_close(&trace);
	return 1;
}

void BTTraceDumpToFile(void)
{
	if(SwitchTraceDumpComplete)
		return;
	if(BTSwitchTraceWriteFile())
		SwitchTraceDumpComplete = 1;
}

static void BTSwitchTraceAutoDumpUpdate(void)
{
	if(!SwitchTraceCaptureStarted || SwitchTraceDumpComplete ||
		SwitchTraceDumpAttempts >= SWITCH_TRACE_DUMP_MAX_ATTEMPTS ||
		TimerDiffSeconds(SwitchTraceCaptureTimer) <
			SWITCH_TRACE_AUTO_DUMP_SECONDS)
		return;
	if(SwitchTraceDumpAttempts &&
		TimerDiffSeconds(SwitchTraceDumpRetryTimer) <
			SWITCH_TRACE_DUMP_RETRY_SECONDS)
		return;
	SwitchTraceDumpAttempts++;
	SwitchTraceDumpRetryTimer = read32(HW_TIMER);
	if(BTSwitchTraceWriteFile())
		SwitchTraceDumpComplete = 1;
}

void BTUpdateRegisters(void)
{
	if(inited == 0)
		return;

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
	sync_before_read(SwitchPpcDiag, sizeof(struct SwitchProPpcDiag));
	if(SwitchPpcDiag->magic == SWITCH_PRO_DIAG_MAGIC &&
		SwitchPpcDiag->selftest_a_seen &&
		BTDiagnosticStage >= BT_DIAG_INPUT_RECEIVED)
		BTDiagnosticStage = BT_DIAG_PPC_SELFTEST;
	if(((BTDiagnosticStage == BT_DIAG_AUTHENTICATED ||
		BTDiagnosticStage == BT_DIAG_ENCRYPTED) &&
		TimerDiffTicks(BTDiagnosticBlinkTimer) > 949220) ||
		((BTDiagnosticStage == BT_DIAG_AUTH_FAILED ||
		BTDiagnosticStage == BT_DIAG_ENCRYPT_FAILED) &&
		TimerDiffTicks(BTDiagnosticBlinkTimer) > 569532) ||
		(BTDiagnosticStage == BT_DIAG_PROTOCOL_READY &&
		TimerDiffTicks(BTDiagnosticBlinkTimer) > 379688) ||
		(BTDiagnosticStage == BT_DIAG_INPUT_RECEIVED &&
		TimerDiffTicks(BTDiagnosticBlinkTimer) > 189844))
	{
		BTDiagnosticBlinkOn ^= 1;
		BTDiagnosticBlinkTimer = read32(HW_TIMER);
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
			if(BTPadConnected[i]->transfertype == TRANSFER_SWITCH_PRO)
				BTSwitchTraceArm(SWITCH_TRACE_ARM_CHANNEL, LastChan,
					CurChan, LastRumble, CurRumble,
					BTPadConnected[i]->controller,
					BTPadConnected[i]->switch_input_valid,
					BTPadConnected[i]->switch_input_reports,
					BTPadConnected[i]->switch_publish_count, 0);
			if(CurChan == CHAN_NOT_SET || ((LastChan != CHAN_NOT_SET) && CurChan < LastChan))
			{
				BTPad[LastChan].used = C_NOT_SET;
				sync_after_write(&BTPad[LastChan], sizeof(struct BTPadCont));
			}
			BTPadConnected[i]->channel = CurChan;
			BTPadConnected[i]->rumble = CurRumble;
			if(BTPadConnected[i]->transfertype == TRANSFER_SWITCH_PRO)
			{
				/* Rumble is intentionally deferred for the first playable build. */
			}
			else if(BTPadConnected[i]->transfertype == 0x3D || BTPadConnected[i]->controller & (C_RUMBLE_WM | C_NUN) || ConfigGetConfig(NIN_CFG_CC_RUMBLE))
				BTSetControllerState(BTPadConnected[i]->sock, LEDState[CurChan] | CurRumble);
			else //classic controller doesnt have rumble, can be forced to wiimote if wanted
				BTSetControllerState(BTPadConnected[i]->sock, LEDState[CurChan]);
			BTPadConnected[i]->diagnostic_state = 0xFFFFFFFF;
			sync_after_write(BTPadConnected[i], sizeof(struct BTPadStat));
		}
		if(BTPadConnected[i]->transfertype == TRANSFER_SWITCH_PRO &&
			CurChan != CHAN_NOT_SET && LastChan != CurChan)
		{
			/* The stream may go quiet immediately after the three reports that
			 * made it ready. Publish the cached latest state as soon as PADReadGC
			 * assigns a real channel, rather than waiting for another packet. */
			if(BTPadConnected[i]->switch_selftest_state == 0)
			{
				/* Decisive ARM->PPC boundary check: pulse GameCube A for half a
				 * second once, then return permanently to live controller data. */
				BTPadConnected[i]->switch_selftest_state = 1;
				BTPadConnected[i]->switch_selftest_timer = read32(HW_TIMER);
				BTSwitchTraceArm(SWITCH_TRACE_ARM_SELFTEST, 1, CurChan,
					0, 0, 0, 0, 0, 0, 0);
			}
			BTSwitchPublishInput(BTPadConnected[i]);
			if(BTPadConnected[i]->switch_led_channel != CurChan)
			{
				u8 player_led = 1 << CurChan;
				BTSwitchSendSubcommand(BTPadConnected[i],
					SWITCH_PRO_SUBCMD_PLAYER_LED, &player_led, 1);
				BTPadConnected[i]->switch_led_channel = CurChan;
				sync_after_write(BTPadConnected[i],
					sizeof(struct BTPadStat));
			}
		}
		if(BTPadConnected[i]->transfertype == TRANSFER_SWITCH_PRO &&
			BTPadConnected[i]->switch_selftest_state == 1)
		{
			if(TimerDiffTicks(BTPadConnected[i]->switch_selftest_timer) < 949220)
				BTSwitchPublishInput(BTPadConnected[i]);
			else
			{
				BTPadConnected[i]->switch_selftest_state = 2;
				BTSwitchTraceArm(SWITCH_TRACE_ARM_SELFTEST, 2,
					BTPadConnected[i]->channel, 0, 0, 0, 0, 0, 0, 0);
				BTSwitchPublishInput(BTPadConnected[i]);
				sync_after_write(BTPadConnected[i], sizeof(struct BTPadStat));
			}
		}
		if(BTPadConnected[i]->transfertype == TRANSFER_SWITCH_PRO)
			BTSwitchUpdateProtocol(BTPadConnected[i]);
		if(BTDiagnosticStage && BTPadConnected[i]->transfertype != TRANSFER_SWITCH_PRO)
		{
			u32 visible_stage = (BTDiagnosticStage == BT_DIAG_HID_OPEN &&
				BTDiagnosticAuthRequested) ? BT_DIAG_AUTH_REQUESTED : BTDiagnosticStage;
			u32 diagnostic_state;
			if(visible_stage == BT_DIAG_INPUT_RECEIVED &&
				BTDiagnosticSwitchChannel < CHAN_NOT_SET)
			{
				u32 buttons = BTDiagnosticSwitchButtons;
				/* Once a BTPad write has happened, show the assigned channel
				 * while idle and give face buttons unmistakable live patterns. */
				diagnostic_state = LEDState[BTDiagnosticSwitchChannel];
				if(buttons & SWITCH_PRO_BTN_A)
					diagnostic_state = 0xF0;
				else if(buttons & SWITCH_PRO_BTN_B)
					diagnostic_state = 0x30;
				else if(buttons & SWITCH_PRO_BTN_X)
					diagnostic_state = 0xC0;
				else if(buttons & SWITCH_PRO_BTN_Y)
					diagnostic_state = 0x90;
				diagnostic_state |= CurRumble;
			}
			else
				diagnostic_state = SwitchProDiagnosticLED(visible_stage,
					BTDiagnosticBlinkOn) | CurRumble;
			if(BTPadConnected[i]->diagnostic_state != diagnostic_state)
			{
				BTSetControllerState(BTPadConnected[i]->sock, diagnostic_state);
				BTPadConnected[i]->diagnostic_state = diagnostic_state;
				sync_after_write(BTPadConnected[i], sizeof(struct BTPadStat));
			}
		}
	}
	if(TimerDiffSeconds(BTTimer) > 0)
	{
		//dbgprintf("tick\n");
		l2cap_tmr(); //every second
		BTTimer = read32(HW_TIMER);
	}
	/* Inquiry and incoming page scan cannot be relied on concurrently on this
	 * controller.  Prefer the stored-key A-wake route for the initial window;
	 * between discovery rounds return to page scan long enough for an incoming
	 * controller to page the host. */
	if(!SwitchInquiryTargetFound && !SwitchInquiryActive &&
		SwitchInquiryAttempts < SWITCH_INQUIRY_MAX_ATTEMPTS &&
		TimerDiffSeconds(SwitchInquiryRetryTimer) >=
			(SwitchInquiryAttempts == 0 ?
				SWITCH_INITIAL_PAGE_WINDOW_SECONDS :
				SWITCH_INQUIRY_PAGE_WINDOW_SECONDS))
	{
		SwitchInquiryActive = 1;
		SwitchInquiryAttempts++;
		BTE_InquiryAsync(CONF_PAD_MAX_REGISTERED, BTPairInquiryCB);
	}
	BTSwitchTraceAutoDumpUpdate();
}
