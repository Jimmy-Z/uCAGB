#include <stdio.h>

#include "pl_win.h"
#include "../common/cmd.h"
#include "gba.h"
#include "gbaencryption.h"

static uint xfer32(tDev d, uint data){
	u8 *p = (u8 *)&data;
	u8 c[5];
	c[0] = CMD_XFER_RW;
	c[1] = p[3]; c[2] = p[2]; c[3] = p[1]; c[4] = p[0];
	write_serial(d, c, 5);
	read_serial(d, c, 4);
	p[0] = c[3]; p[1] = c[2]; p[2] = c[1]; p[3] = c[0];
	return data;
}

static void xfer32wo(tDev d, uint data){
	u8 *p = (u8 *)&data;
	u8 c[5];
	c[0] = CMD_XFER_W;
	c[1] = p[3]; c[2] = p[2]; c[3] = p[1]; c[4] = p[0];
	write_serial(d, c, 5);
	return;
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
	xfer16(d, 0x6100);
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

static int gba_send_main(tDev d, const u8 *rom, tSize size){
	uint ret, offset, timeout;
	struct gbaCrcState crc;
	struct gbaEncryptionState enc;

	ret = xfer16(d, 0x6202);
	fprintf(stderr, "sending command: received 0x%04x\n", ret);

	gba_exchange_keys(d, size, &crc, &enc);

	for(offset = 0xc0; offset < size; offset += 4){
		u32 data = ((u32 *)rom)[offset >> 2];

		gbaCrcAdd(data, &crc);
		data = gbaEncrypt(data, offset, &enc);

#if 0 // huge speed gain if we just do not get any return from the serial
		ret = xfer32(d, data);
		// percentage indicator code is commented out purposely, windows console performance sucks
		// fprintf(stderr, "\rmain block (%d%%): receive 0x%08x", (offset * 100) / size, ret);
		if((ret >> 16) != (offset & 0xffff)){
			fprintf(stderr, "\ntransmission error\n");
			return -1;
		}
#else
		xfer32wo(d, data);
		// fprintf(stderr, "\rmain block (%d%%)", (offset * 100) / size);
#endif
	}

	ret = xfer16(d, 0x0065);
	fprintf(stderr, "\rmain block complete: received 0x%04x\n", ret);

	timeout = 0x10;
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

int gba_multiboot(tDev d, const u8 *rom, tSize size){
	gba_send_header(d, rom);
	return gba_send_main(d, rom, size);
}

