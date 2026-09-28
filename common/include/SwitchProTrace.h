#ifndef _SWITCH_PRO_TRACE_H_
#define _SWITCH_PRO_TRACE_H_

#define SWITCH_PRO_TRACE_MAGIC 0x53505452
#define SWITCH_PRO_TRACE_VERSION 1
#define SWITCH_PRO_TRACE_EVENTS 80
#define SWITCH_PRO_TRACE_REGION_SIZE 0x1000

#define SWITCH_PRO_TRACE_ARM_ADDR 0x132F1000
#define SWITCH_PRO_TRACE_PPC_ADDR 0x132F2000

#define SWITCH_TRACE_WRITER_ARM 1
#define SWITCH_TRACE_WRITER_PPC 2

enum SwitchProTraceType {
	SWITCH_TRACE_ARM_INIT = 1,
	SWITCH_TRACE_ARM_PHASE,
	SWITCH_TRACE_ARM_TX_SUBCOMMAND,
	SWITCH_TRACE_ARM_RX_REPORT,
	SWITCH_TRACE_ARM_PARSE,
	SWITCH_TRACE_ARM_PUBLISH,
	SWITCH_TRACE_ARM_CHANNEL,
	SWITCH_TRACE_ARM_SELFTEST,
	SWITCH_TRACE_ARM_INQUIRY,
	SWITCH_TRACE_ARM_HID_HOST,
	SWITCH_TRACE_PPC_READ = 0x100,
	SWITCH_TRACE_PPC_PAD
};

struct SwitchProTraceEvent {
	u32 sequence;
	u32 ticks;
	u32 type;
	u32 data[9];
} __attribute__((aligned(16)));

struct SwitchProTraceBuffer {
	u32 magic;
	u32 version;
	u32 writer;
	u32 count;
	u32 dropped;
	u32 flags;
	u32 reserved[2];
	struct SwitchProTraceEvent events[SWITCH_PRO_TRACE_EVENTS];
} __attribute__((aligned(32)));

typedef char SwitchProTraceEventSize[(sizeof(struct SwitchProTraceEvent) == 48) ? 1 : -1];
typedef char SwitchProTraceBufferFitsRegion[(sizeof(struct SwitchProTraceBuffer) <= SWITCH_PRO_TRACE_REGION_SIZE) ? 1 : -1];

#endif
