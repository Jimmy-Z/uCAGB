typedef unsigned int u32;
typedef unsigned char u8;

#define CRC32_TABLE_LEN 0x100

void init_crc32_table(u32 *crc32_tab);
u32 crc32(u32 *crc32_tab, u32 crc, const u8 *buf, u32 size);
