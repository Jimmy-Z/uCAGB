// these are shared between uCSIO and PC
#define CMD_FLAG_W	1
#define CMD_FLAG_R	2
#define CMD_FLAG_B	4

#define CMD_FLAG_BITS	3
#define CMD_FLAG_MASK	((1 << CMD_FLAG_BITS) - 1)
#define CMD_MASK	(~(uint8_t)CMD_FLAG_MASK)

#define CMD_XFER	(1 << CMD_FLAG_BITS)
#define CMD_PING	(2 << CMD_FLAG_BITS)
#define CMD_BOOTLOADER	(3 << CMD_FLAG_BITS)
#define CMD_COUNTER	(4 << CMD_FLAG_BITS)
#define CMD_SET_WAIT	(5 << CMD_FLAG_BITS)

#define BULK_SIZE 8 // u32[8]

#define AGB_BUF_SIZE 0x20000 // 128K(*u8)

// these are shared between DFAGB and PC
// commands sent to DFAGB from PC
// actually DFAGB FSM only check the 1st byte for a command
// the lower 24 bits can be used as parameter(s)
#define DF_CMD_MASK		0xff000000
#define DF_PARAM_MASK		0x00ffffff
#define DF_CMD_NOP		0x00504f4e // "NOP"
// upload and download (to/from the 128KB EWRAM buffer) are processed by the FSM directly
// the lower 24 bits are transfer length(of u32)
#define DF_CMD_UPLOAD		(1 << 24)
#define DF_CMD_DOWNLOAD		(2 << 24)
// read fsm_p0, like the return value of crc32
#define DF_CMD_READ		(3 << 24)
// these are WORKER commands, FSM will block(return busy) until worker finishes them in main loop
#define DF_CMD_CRC32		(0x10 << 24) // length (of u8)
// save
#define DF_CMD_READ_SRAM	(0x20 << 24)
#define DF_CMD_WRITE_SRAM	(0x21 << 24)
#define DF_CMD_READ_FLASH	(0x22 << 24)
#define DF_CMD_WRITE_FLASH	(0x23 << 24)
#define DF_CMD_READ_EEPROM	(0x24 << 24)
#define DF_CMD_WRITE_EEPROM	(0x25 << 24)
// ROM
#define DF_CMD_DUMP		(0x30 << 24)
#define DF_CMD_VERIFY		(0x31 << 24)
// Flash carts
#define DF_CMD_ID		(0x40 << 24)
#define DF_CMD_UNLOCK		(0x41 << 24)
#define DF_CMD_ERASE		(0x42 << 24)
#define DF_CMD_PROGRAM		(0x43 << 24)

#define MULTIBOOT_PING		0x00006202

// this is the 4CC status code PC will read from DFAGB via SIO
#define DF_STATE_IDLE	0x454c4449 // "IDLE"
#define DF_STATE_BUSY	0x59535542 // "BUSY"

