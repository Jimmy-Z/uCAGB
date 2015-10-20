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
#define CMD_SET_WS	(5 << CMD_FLAG_BITS)
#define CMD_UNSET_WS	(6 << CMD_FLAG_BITS)

#define BULK_SIZE 8 // u32[8]
