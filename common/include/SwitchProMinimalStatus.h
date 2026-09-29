#ifndef _SWITCH_PRO_MINIMAL_STATUS_H_
#define _SWITCH_PRO_MINIMAL_STATUS_H_

#include <stdint.h>

#define SWITCH_PRO_STATUS_MAGIC    0x53504d53u
#define SWITCH_PRO_STATUS_VERSION  1u
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
	uint32_t reserved[17];
} SwitchProMinimalStatus;

typedef char SwitchProMinimalStatusSizeCheck[
	(sizeof(SwitchProMinimalStatus) == 128) ? 1 : -1];
typedef char SwitchProMinimalStatusAddressAliasCheck[
	((SWITCH_PRO_STATUS_PPC_ADDR & 0x1fffffffu) ==
	SWITCH_PRO_STATUS_ARM_ADDR) ? 1 : -1];

#endif
