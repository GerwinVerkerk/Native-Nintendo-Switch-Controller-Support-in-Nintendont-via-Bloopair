#ifndef _SWITCH_PRO_MINIMAL_STATUS_H_
#define _SWITCH_PRO_MINIMAL_STATUS_H_

#include <stdint.h>

#define SWITCH_PRO_STATUS_MAGIC    0x53504d53u
#define SWITCH_PRO_STATUS_VERSION  6u
#define SWITCH_PRO_STATUS_ARM_ADDR 0x132f3040u
#define SWITCH_PRO_STATUS_PPC_ADDR 0x932f3040u
#define SWITCH_PRO_STATUS_PATH     "switch-pro-minimal.bin"

#define SWITCH_PRO_STATUS_PAIRING_VALID  (1u << 0)
#define SWITCH_PRO_STATUS_LISTENER_READY (1u << 1)
#define SWITCH_PRO_STATUS_ACL_CONNECTED  (1u << 2)
#define SWITCH_PRO_STATUS_KEY_REPLIED    (1u << 3)
#define SWITCH_PRO_STATUS_AUTH_REQUESTED (1u << 4)
#define SWITCH_PRO_STATUS_AUTHENTICATED  (1u << 5)
#define SWITCH_PRO_STATUS_ENCRYPT_REQUESTED (1u << 6)
#define SWITCH_PRO_STATUS_ENCRYPTED      (1u << 7)
#define SWITCH_PRO_STATUS_CONTROL_OPEN   (1u << 8)
#define SWITCH_PRO_STATUS_INTERRUPT_OPEN (1u << 9)
#define SWITCH_PRO_STATUS_CONNECTED      (1u << 10)
#define SWITCH_PRO_STATUS_BASIC_SEEN     (1u << 11)
#define SWITCH_PRO_STATUS_BASIC_PARSED   (1u << 12)
#define SWITCH_PRO_STATUS_PUBLISHED      (1u << 13)
#define SWITCH_PRO_STATUS_INIT_STARTED   (1u << 14)
#define SWITCH_PRO_STATUS_DEVICE_INFO    (1u << 15)
#define SWITCH_PRO_STATUS_REPORT_MODE    (1u << 16)
#define SWITCH_PRO_STATUS_INIT_COMPLETE  (1u << 17)
#define SWITCH_PRO_STATUS_FULL_SEEN      (1u << 18)
#define SWITCH_PRO_STATUS_FULL_PARSED    (1u << 19)
#define SWITCH_PRO_STATUS_INIT_RETRIED   (1u << 20)
#define SWITCH_PRO_STATUS_INIT_FAILED    (1u << 21)
#define SWITCH_PRO_STATUS_TRANSPORT_PENDING (1u << 22)
#define SWITCH_PRO_STATUS_TRANSPORT_READY   (1u << 23)
#define SWITCH_PRO_STATUS_TRANSPORT_TIMEOUT (1u << 24)
#define SWITCH_PRO_STATUS_CONTROL_DEDICATED  (1u << 25)
#define SWITCH_PRO_STATUS_INTERRUPT_DEDICATED (1u << 26)
#define SWITCH_PRO_STATUS_DUPLICATE_LISTENER (1u << 27)

#define SWITCH_PRO_STATUS_OWNER_NONE       0u
#define SWITCH_PRO_STATUS_OWNER_DEDICATED  1u
#define SWITCH_PRO_STATUS_OWNER_REGULAR_BASE 0x100u
#define SWITCH_PRO_STATUS_OWNER_UNKNOWN    0xffffffffu

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t flags;
	int32_t last_error;
	uint32_t listener_result;
	uint32_t acl_count;
	uint32_t key_requests;
	uint32_t auth_result;
	uint32_t encrypt_result;
	uint32_t control_count;
	uint32_t interrupt_count;
	uint32_t basic_reports;
	uint32_t parsed_reports;
	uint32_t publishes;
	uint32_t channel;
	uint32_t command_reports;
	uint32_t init_sent;
	uint32_t init_acks;
	uint32_t init_retries;
	uint32_t full_reports;
	uint32_t init_index;
	uint32_t bte_state;
	uint32_t control_l2cap_state;
	uint32_t interrupt_l2cap_state;
	uint32_t transport_checks;
	uint32_t transport_deferred;
	uint32_t init_send_attempts;
	int32_t init_last_send_result;
	uint32_t transport_timeouts;
	uint32_t stored_address_matches;
	uint32_t control_channel_owner;
	uint32_t interrupt_channel_owner;
	uint32_t led_desired_mask;
	uint32_t led_sent_mask;
	uint32_t led_acks;
	uint32_t led_send_attempts;
	uint32_t slot_count;
	uint32_t slot_channel[4];
	uint32_t slot_init_acks[4];
	uint32_t slot_published_mask;
	uint32_t slot_connected_mask;
	uint32_t reserved;
} SwitchProMinimalStatus;

typedef char SwitchProMinimalStatusSizeCheck[
	(sizeof(SwitchProMinimalStatus) == 192) ? 1 : -1];
typedef char SwitchProMinimalStatusAddressAliasCheck[
	((SWITCH_PRO_STATUS_PPC_ADDR & 0x1fffffffu) ==
	SWITCH_PRO_STATUS_ARM_ADDR) ? 1 : -1];

#endif
