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

// these are shared between DFAGB and PC
// commands sent to DFAGB from PC, the lower 24 bits are used as 2 parameters
// one 8 bit and one 16 bit
// upload and download (to/from the 128KB buffer) are processed by the SIO FSM
#define DF_CMD_UPLOAD	(1 << 24)
#define DF_CMD_DOWNLOAD	(2 << 24)
// actually DFAGB FSM only check the 1st byte
// so make sure the higher 8 bit is not a valid command
#define DF_CMD_NOP	0x00504f4e
// these are WORKER commands, SIO FSM will transfer them to WORKER FSM
#define DF_CMD_FLASH	(3 << 24)
#define DF_CMD_DUMP	(4 << 24)
#define DF_CMD_READ_SRAM	(5 << 24)
#define DF_CMD_WRITE_SRAM	(6 << 24)
#define DF_CMD_READ_FLASH	(7 << 24)
#define DF_CMD_WRITE_FLASH	(8 << 24)
#define DF_CMD_READ_EEPROM	(9 << 24)
#define DF_CMD_WRITE_EEPROM	(0x0a << 24)

// this is the 4CC status code PC will read from DFAGB via SIO
#define DF_STATE_IDLE	0x454c4449
#define DF_STATE_BUSY	0x59535542
