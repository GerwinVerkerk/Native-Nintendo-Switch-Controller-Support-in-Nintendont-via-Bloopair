#ifndef _SWITCH_PRO_PAIRING_H_
#define _SWITCH_PRO_PAIRING_H_

#include <stddef.h>
#include <stdint.h>

#define SWITCH_PRO_PAIRING_MAGIC   0x4e535042u
#define SWITCH_PRO_PAIRING_VERSION 1u
#define SWITCH_PRO_PAIRING_ADDR    0x132f3000u
#define SWITCH_PRO_PAIRING_PATH    "wiiu/bloopair/nintendont-switch-pro.bin"
#define SWITCH_PRO_PAIRING_TYPE    0x24u

typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint8_t controller_bda[6];
	uint8_t console_bda[6];
	uint8_t link_key[16];
	uint8_t key_type;
	uint8_t controller_type;
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t reserved[2];
	uint32_t checksum;
} SwitchProPairing;

static inline uint32_t SwitchProPairingChecksum(const SwitchProPairing *pairing)
{
	const uint8_t *bytes = (const uint8_t*)pairing;
	uint32_t hash = 2166136261u;
	size_t i;
	for(i = 0; i < offsetof(SwitchProPairing, checksum); i++) {
		hash ^= bytes[i];
		hash *= 16777619u;
	}
	return hash;
}

static inline int SwitchProPairingIsValid(const SwitchProPairing *pairing)
{
	uint8_t key_or = 0;
	uint8_t controller_or = 0;
	uint8_t console_or = 0;
	size_t i;
	if(pairing == NULL || pairing->magic != SWITCH_PRO_PAIRING_MAGIC ||
		pairing->version != SWITCH_PRO_PAIRING_VERSION ||
		pairing->size != sizeof(*pairing) ||
		pairing->controller_type != SWITCH_PRO_PAIRING_TYPE ||
		pairing->vendor_id != 0x057e ||
		pairing->product_id != 0x2009)
		return 0;
	for(i = 0; i < sizeof(pairing->controller_bda); i++)
	{
		controller_or |= pairing->controller_bda[i];
		console_or |= pairing->console_bda[i];
	}
	for(i = 0; i < sizeof(pairing->link_key); i++)
		key_or |= pairing->link_key[i];
	return controller_or != 0 && console_or != 0 && key_or != 0 &&
		pairing->checksum == SwitchProPairingChecksum(pairing);
}

typedef char SwitchProPairingSizeCheck[(sizeof(SwitchProPairing) == 48) ? 1 : -1];

#endif
