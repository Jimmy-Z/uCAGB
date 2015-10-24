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
#define DF_CMD_NOP		0x00504f4e
// upload and download (to/from the 128KB buffer) are processed by the FSM directly
// the lower 24 bits are transfer length(of u32)
#define DF_CMD_UPLOAD		(1 << 24)
#define DF_CMD_DOWNLOAD		(2 << 24)
// read fsm_p0, like the return value of crc32
#define DF_CMD_READ		(3 << 24)
// these are WORKER commands, FSM will block(return busy) until worker finishes them in main thread
#define DF_CMD_CRC32		(0x10 << 24) // length (of u8)
#define DF_CMD_FLASH		(0x11 << 24)
#define DF_CMD_DUMP		(0x12 << 24)
#define DF_CMD_READ_SRAM	(0x13 << 24)
#define DF_CMD_WRITE_SRAM	(0x14 << 24)
#define DF_CMD_READ_FLASH	(0x15 << 24)
#define DF_CMD_WRITE_FLASH	(0x16 << 24)
#define DF_CMD_READ_EEPROM	(0x17 << 24)
#define DF_CMD_WRITE_EEPROM	(0x18 << 24)

// this is the 4CC status code PC will read from DFAGB via SIO
#define DF_STATE_IDLE	0x454c4449
#define DF_STATE_BUSY	0x59535542
