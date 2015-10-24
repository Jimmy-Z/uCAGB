#include <stdio.h>
#include <stdlib.h>

#include "../common/common.h"
#include "../common/crc32.h"
#include "pl.h"
#include "gba.h"

u32 crc32_table[CRC32_TABLE_LEN];

uint align(uint a, uint b){
	return (a + b - 1) & (~(b - 1));
}

unsigned char *load_file(const char *filename, tSize *psize, tSize a){
	tSize size;
	FILE *f;
	unsigned char *data;
	f = fopen(filename, "rb");
	if(!f){
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	*psize = align(size, a);
	data = malloc(*psize);
	memset(data, 0, *psize);
	fseek(f, 0, SEEK_SET);
	fread(data, 1, size, f);
	fclose(f);
	return data;
}

void save_file(const char *filename, const void *buf, tSize size){
	FILE *f;
	f = fopen(filename, "wb");
	if(!f){
		fprintf(stderr, "failed to open \"%s\" for write\n", filename);
		return;
	}
	fwrite(buf, size, 1, f);
	fclose(f);
}

void set_wait(tDev d, u8 wait_p0, u8 wait_p1){
	u8 c[] = {CMD_SET_WAIT | CMD_FLAG_W, wait_p0, wait_p1, 0, 0};
	fprintf(stderr, "set_wait(%d, %d)\n", wait_p0, wait_p1);
	write_serial(d, &c, 5);
}

int multiboot(tDev d, const char *filename){
	char *rom;
	tSize size;
	u8 c;
	uint t;

	rom = load_file(filename, &size, BULK_SIZE << 2);
	if(rom == NULL){
		fprintf(stderr, "can't open %s\n", filename);
		return -2;
	}

	set_wait(d, 0, 0);

	if(gba_ready(d)){
		return -3;
	}

	t = get_rtime();
	if(gba_multiboot(d, rom, size)){
		return -4;
	}

	t = get_rtime() - t;
	fprintf(stderr, "transfer time: %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		t / 1000.0, size * 8.0 / t, size * 1.0 / t);

	return 0;
}


void reset_to_bootloader(tDev d){
	u8 c = CMD_BOOTLOADER;
	write_serial(d, &c, 1);
	fprintf(stderr, "reset to bootloader command sent\n");
	return;
}

int serial_bench(tDev d, int mode, int length){
	uint i, t0, dt;
	u8 c[BULK_SIZE * 5], *p = 0;
	const char *mode_str;

	length = align(length, BULK_SIZE << 2);
	c[0] = CMD_COUNTER;
	write_serial(d, c, 1);

	fprintf(stderr, "starting %d bytes serial speed test\n", length);
	t0 = get_rtime();
	if(mode == 0){
		// 4 bytes per write, the simplest and slowest
		mode_str = "32 bit";
		c[0] = CMD_FLAG_W;
		c[1] = 0; c[2] = 0; c[3] = 0; c[4] = 0;
		for(i = 0; i < length / 4; ++i){
			write_serial(d, c, 5);
		}
	}else if(mode == 1){
		// transfer the entire file with a single write, massive performance improve
		// only works with the simplified benchmark only uC firmware
		// but if the file goes to big, WriteFile will fail
		mode_str = "single bulk";
		p = malloc(length / 4 * 5);
		memset(p, 0, length / 4 * 5);
		for(i = 0; i < length / 4; ++i){
			p[i * 5] = CMD_FLAG_W;
		}
		write_serial(d, p, length / 4 * 5);
	}else if(mode == 2){
		// BULK_SIZE per write, performance close to mode 1 in this test
		// but overall real world multiboot performance is only a tiny bit better than mode 0
		mode_str = "32 bytes semi bulks";
		memset(c, 0, BULK_SIZE * 5);
		for(i = 0; i < BULK_SIZE; ++i){
			c[i * 5] = CMD_FLAG_W;
		}
		for(i = 0; i < length / BULK_SIZE / 4; ++i){
			write_serial(d, c, BULK_SIZE * 5);
		}
	}else if(mode = 3){
		// similar to mode 2 but uses a single CMD_WB instead of a series of CMD_W
		// better than mode 2, I guess that's because the reduced write amp(5/4 -> 33/32)
		// but multiboot need a huge wait between xfer, overall multiboot performance worse than mode 2
		mode_str = "32 bytes bulks";
		c[0] = CMD_FLAG_W | CMD_FLAG_B;
		for(i = 0; i < length / BULK_SIZE / 4; ++i){
			write_serial(d, c, 1 + (BULK_SIZE << 2));
		}
	}

	dt = get_rtime() - t0;
	fprintf(stderr, "%s mode, transfer time: %.2f seconds, average speed %.2f Kbps, %.2f KB/s\n",
		mode_str, dt / 1000.0, length *  8.0 / dt, length * 1.0 / dt);

	if(p != 0){
		free(p);
	}

	// read the counter
	c[0] = CMD_COUNTER | CMD_FLAG_R | CMD_FLAG_B;
	write_serial(d, c, 1);
	read_serial(d, c, BULK_SIZE << 2);
	fprintf(stderr, "uC counter: r = %d, w = %d, x = %d\n",
		((u32*)c)[0], ((u32*)c)[1], ((u32*)c)[2]);

	return 0;
}

void df_wait(tDev d, const char * msg){
	u32 r;
	do{
		r = xfer32(d, DF_CMD_NOP);
		sleep(1000/0x10);
		fprintf(stderr, "\rwaiting for %s, response: 0x%08x", msg, r);
	}while(r != DF_STATE_IDLE);
	fprintf(stderr, "\n%s ready\n", msg);
}

int df_test(tDev d, int mode, unsigned seed){
	u8 buf[AGB_BUF_SIZE];
	unsigned i, crc, t;
	set_wait(d, 1, 0);
	// fprintf(stderr, "RAND_MAX = 0x%08x\n", RAND_MAX);
	srand(seed);
	for(i = 0; i < AGB_BUF_SIZE; ++i){
		buf[i] = rand() & 0xff;
	}
	crc = crc32(crc32_table, 0, buf, AGB_BUF_SIZE);
	// save_file("128K.a.bin", buf, AGB_BUF_SIZE);
	fprintf(stderr, "random buffer CRC32: 0x%08x\n", crc);

	df_wait(d, "DFAGB");
	fprintf(stderr, "uploading to DFAGB...\n");
	t = get_rtime();
	xfer32wo(d, DF_CMD_UPLOAD | (AGB_BUF_SIZE >> 2));
	xfer32bw(d, buf, AGB_BUF_SIZE);
	t = get_rtime() - t;
	fprintf(stderr, "upload to DFAGB complete, %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		t / 1000.0, AGB_BUF_SIZE * 8.0 / t, AGB_BUF_SIZE * 1.0 / t);

	xfer32wo(d, DF_CMD_CRC32 | AGB_BUF_SIZE);
	df_wait(d, "CRC32");
	xfer32wo(d, DF_CMD_READ);
	crc = xfer32ro(d);
	fprintf(stderr, "DFAGB returned CRC32: 0x%08x\n", crc);

	fprintf(stderr, "downloading from DFAGB...\n");

	t = get_rtime();
	xfer32wo(d, DF_CMD_DOWNLOAD | (AGB_BUF_SIZE >> 2));
	xfer32br(d, buf, AGB_BUF_SIZE);
	t = get_rtime() - t;
	fprintf(stderr, "download from DFAGB complete, %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		t / 1000.0, AGB_BUF_SIZE * 8.0 / t, AGB_BUF_SIZE * 1.0 / t);
	// save_file("128K.b.bin", buf, AGB_BUF_SIZE);
	crc = crc32(crc32_table, 0, buf, AGB_BUF_SIZE);
	fprintf(stderr, "buffer CRC32: 0x%08x\n", crc);

	return 0;
}

#define PING_PATTERN 0xff00aa55
int validate_uC(tDev d){
	u8 c[5];
	c[0] = CMD_PING | CMD_FLAG_W | CMD_FLAG_R;
	*((u32*)&c[1]) = PING_PATTERN;
	write_serial(d, c, 5);
	read_serial(d, c, 4);
	fprintf(stderr, "ping returned 0x%08x, expecting 0x%08x\n", *((u32*)c), ~(u32)PING_PATTERN);
	return (*((u32*)c)) ^ (~(u32)PING_PATTERN);
}

int main(int argc, const char *argv[]){
	tDev d;

	init_crc32_table(crc32_table);

	if(argc < 2){
		fprintf(stderr, "you should at least specify a serial device name\n");
		return -1;
	}

	d = open_serial(argv[1]);
	if(validate_serial(d)){
		fprintf(stderr, "can't open %s\n", argv[1]);
		return -1;
	}
	fprintf(stderr, "open %s success\n", argv[1]);

	// setup_serial(d);

	if(validate_uC(d)){
		fprintf(stderr, "ping %s failed\n", argv[1]);
		return -1;
	}
	fprintf(stderr, "ping %s success\n", argv[1]);

	if(argc == 4 && !strcmp(argv[2], "multiboot")){
		return multiboot(d, argv[3]);
	}else if(argc == 3 && !strcmp(argv[2], "bootloader")){
		return reset_to_bootloader(d);
	}else if(argc == 5 && !strcmp(argv[2], "test")){
		return serial_bench(d, atoi(argv[3]), atoi(argv[4]));
	}else if(argc == 5 && !strcmp(argv[2], "df")){
		return df_test(d, atoi(argv[3]), strtoul(argv[4], NULL, 0x10));
	}else{
		fprintf(stderr, "invalid parameters, example:\n\t%s COM1 multiboot your_file.gba\n", argv[0]);
		return -1;
	}
}

