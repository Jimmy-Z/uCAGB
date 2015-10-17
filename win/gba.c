#include <stdio.h>

#include "pl_win.h"
#include "../common/cmd.h"
#include "gba.h"
#include "gbaencryption.h"

u32 xfer32(tDev d, u32 data){
	u8 c[5];
	c[0] = CMD_FLAG_W | CMD_FLAG_X | CMD_FLAG_R;
	*(u32*)&c[1] = data;
	write_serial(d, c, 5);
	read_serial(d, c, 4);
	return *(u32*)c;
}

void xfer32wo(tDev d, u32 data){
	u8 c[5];
	c[0] = CMD_FLAG_W | CMD_FLAG_X;
	*(u32*)&c[1] = data;
	write_serial(d, c, 5);
	return;
}

#define BULK_SIZE 0x20
void xfer32bulk(tDev d, u8* data, tSize size){
	u8 c[BULK_SIZE / 4 * 5];
	uint i, j;
	for(i = 0; i < BULK_SIZE / 4; ++i){
		c[i * 5] = CMD_FLAG_W | CMD_FLAG_X;
		// c[i * 5] = CMD_BULK;
	}
	for(i = 0; i < size / BULK_SIZE; ++i){
		for(j = 0; j < BULK_SIZE / 4; ++j){
			*(u32*)(c + j * 5 + 1) = *(u32*)(data + i * BULK_SIZE + j * 4);
		}
		write_serial(d, c, BULK_SIZE / 4 * 5);
	}
	return;
}

u32 xfer32ro(tDev d){
	u8 c[4];
	c[0] = CMD_FLAG_X | CMD_FLAG_R;
	write_serial(d, c, 1);
	read_serial(d, c, 4);
	return *(u32*)c;
}

static uint xfer16(tDev d, uint data){
	return xfer32(d, data & 0xffff) >> 16;
}

static void xfer16wo(tDev d, uint data){
	xfer32wo(d, data & 0xffff);
}

int gba_ready(tDev d){
	uint ret, timeout = 0x100;
	do {
		ret = xfer16(d, 0x6202);
		fprintf(stderr, "\rwaiting: received 0x%04x", ret);
		--timeout;
		sleep(1000/0x10);
	}while(ret != 0x7202 && timeout);
	if(ret != 0x7202){
		fprintf(stderr, "\nwaiting timeout\n");
		return -1;
	}
	fprintf(stderr, "\nready\n");
	return 0;
}

static int gba_send_header(tDev d, const u8 *header){
	uint i, ret;
	xfer16wo(d, 0x6100);
	fprintf(stderr, "sending header...\n");
	for (i = 0; i < 0x60; ++i){
		xfer16wo(d, ((u16 *)header)[i]);
		// fprintf(stderr, "\rheader (%d%%): received 0x%04x", (i * 100) / 0x60, ret);
	}
	ret = xfer16(d, 0x6200);
	fprintf(stderr, "\rheader complete: received 0x%04x\n", ret);
	return 0;
}

#define P_COLOR 1
#define P_SPEED 1
#define P_DIR 1

static void gba_exchange_keys(tDev d, uint size, struct gbaCrcState *pcrc, struct gbaEncryptionState *penc){
	uint ret, seed, hh, rr;
	u8 pp = 0x81 + P_COLOR * 0x10 + P_DIR * 0x8 + P_SPEED * 0x2;

	ret = xfer16(d, 0x6300 | pp);
	fprintf(stderr, "send encryption key: received 0x%04x\n", ret);

	ret = xfer16(d, 0x6300 | pp);
	fprintf(stderr, "get encryption key: received 0x%04x\n", ret);
	if ((ret >> 8) != 0x73){
		return;
	}

	seed = (ret << 8) | 0xffff0000 | pp;
	hh = (ret + 0x0f) & 0xff;

	ret = xfer16(d, 0x6400 | hh);
	fprintf(stderr, "encryption confirmation: received 0x%04x\n", ret);

	sleep(1000/16);

	ret = xfer16(d, ((size - 0xc0) >> 2) - 0x34);
	fprintf(stderr, "size exchange: received 0x%04x\n", ret);
	rr = ret & 0xff;

	gbaCrcInit(hh, rr, pcrc);
	gbaEncryptionInit(seed, penc);
}

static int gba_send_main(tDev d, u8 *rom, tSize size){
	uint ret, offset, timeout;
	u32 *p;
	struct gbaCrcState crc;
	struct gbaEncryptionState enc;

	ret = xfer16(d, 0x6202);
	fprintf(stderr, "sending command: received 0x%04x\n", ret);

	gba_exchange_keys(d, size, &crc, &enc);

#define USE_BULK 1
#if USE_BULK
	fprintf(stderr, "encrypting main block...\n");
#else
	fprintf(stderr, "encrypting and sending main block...\n");
#endif
	for(offset = 0xc0, p = (u32*)&rom[offset]; offset < size; offset += 4, ++p){
		gbaCrcAdd(*p, &crc);
		*p = gbaEncrypt(*p, offset, &enc);
#if USE_BULK
	}
	fprintf(stderr, "sending main block...\n");
	xfer32bulk(d, rom + 0xc0, size - 0xc0);
#else
		xfer32wo(d, *p);
	}
#endif


	ret = xfer16(d, 0x0065);
	fprintf(stderr, "\rmain block complete: received 0x%04x\n", ret);

	timeout = 0x20;
	do{
		ret = xfer16(d, 0x0065);
		fprintf(stderr, "\rchecksum wait: received 0x%04x", ret);
		--timeout;
		sleep(1000/0x10);
	}while(ret != 0x0075 && timeout);
	if(ret != 0x0075){
		fprintf(stderr, "\nchecksum waiting timeout\n");
		return -1;
	}

	ret = xfer16(d, 0x0066);
	fprintf(stderr, "\nchecksum tx: received 0x%04x\n", ret);
	gbaCrcFinalize(ret, &crc);

	ret = xfer16(d, crc.crc);
	fprintf(stderr, "checksum rx: received 0x%04x expected 0x%04x\n", ret, crc.crc & 0xffff);

	return 0;
}

int gba_multiboot(tDev d, u8 *rom, tSize size){
	gba_send_header(d, rom);
	return gba_send_main(d, rom, size);
}

