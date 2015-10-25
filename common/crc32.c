#ifdef CRC32TEST
#include <stdlib.h>
#include <stdio.h>
#endif

#include "crc32.h"

// http://stackoverflow.com/questions/26049150/calculate-a-32-bit-crc-lookup-table-in-c-c
void init_crc32_table(u32 *crc32_table){
	u32 p = 0xedb88320, r, b = 0, bit;
	do{
		r = b;
		for(bit = 8; bit > 0; --bit){
			if(r & 1){
				r = (r >> 1) ^ p;
			}else{
				r = r >> 1;
			}
		}
		*crc32_table++ = r;
	}while(++b < CRC32_TABLE_LEN);
}

// http://www.opensource.apple.com/source/xnu/xnu-1456.1.26/bsd/libkern/crc32.c
u32 crc32(u32 *crc32_table, u32 crc, const void *buf, u32 size){
	const u8 *p;
	p = buf;
	crc = ~crc;
	while(size--){
		crc = crc32_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
	}
	return ~crc;
}

#ifdef CRC32TEST
static u32 crc32_table[CRC32_TABLE_LEN];
int main(int argc, const char* argv[]){
	size_t size;
	FILE *f;
	u8 *buf;
	u32 crc;
	int i;

	init_crc32_table(crc32_table);

	if(argc < 2){
		for(i = 0; i < CRC32_TABLE_LEN; i += 8){
			printf("%08x %08x %08x %08x %08x %08x %08x %08x\n",
				crc32_table[i],
				crc32_table[i + 1],
				crc32_table[i + 2],
				crc32_table[i + 3],
				crc32_table[i + 4],
				crc32_table[i + 5],
				crc32_table[i + 6],
				crc32_table[i + 7]);
		}
		return -1;
	}

	f = fopen(argv[1], "rb");
	if(!f){
		fprintf(stderr, "failed to open file\n");
		return -1;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	buf = malloc(size);
	fseek(f, 0, SEEK_SET);
	fread(buf, 1, size, f);
	fclose(f);

	crc = crc32(crc32_table, 0, buf, size);
	printf("%08x", crc);
}
#endif
