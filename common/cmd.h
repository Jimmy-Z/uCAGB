#define CMD_FLAG_W		1	// write, from PC side of view
#define CMD_FLAG_R		2	// read
#define CMD_FLAG_X		4	// do transfer with GBA
#define CMD_PING		((1 << 3)|CMD_FLAG_W|CMD_FLAG_R)
#define CMD_BOOTLOADER		(2 << 3)
