#include <stdio.h>
#include <stdlib.h>

#include "pl_win.h"
#include "../common/common.h"
#include "gba.h"

uint align(uint a, uint b){
	return (a + b - 1) & (~(b - 1));
}

unsigned char *load_rom(const char *filename, tSize *psize){
	tSize size;
	FILE *f;
	unsigned char *data;
	f = fopen(filename, "rb");
	if(!f){
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	*psize = align(size, BULK_SIZE << 2);
	data = malloc(*psize);
	memset(data, 0, *psize);
	fseek(f, 0, SEEK_SET);
	fread(data, 1, size, f);
	fclose(f);
	return data;
}

int multiboot(tDev d, const char *filename){
	char *rom;
	tSize size;
	uint t0, dt;

	rom = load_rom(filename, &size);
	if(rom == NULL){
		fprintf(stderr, "can't open %s\n", filename);
		return -2;
	}

	if(gba_ready(d)){
		return -3;
	}

	t0 = get_rtime();
	if(gba_multiboot(d, rom, size)){
		return -4;
	}

	dt = get_rtime() - t0;
	fprintf(stderr, "transfer time: %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		dt / 1000.0, size * 8.0 / dt, size * 1.0 / dt);

	return 0;
}


void reset_to_bootloader(tDev d){
	u8 c = CMD_BOOTLOADER;
	write_serial(d, &c, 1);
	fprintf(stderr, "jmp_bl command sent\n");
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
		// 4 bytes per write, the slowest
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
		// but only improve real world multiboot performance a tiny bit
		mode_str = "32 bytes semi bulks";
		memset(c, 0, BULK_SIZE * 5);
		for(i = 0; i < BULK_SIZE; ++i){
			c[i * 5] = CMD_FLAG_W;
		}
		for(i = 0; i < length / BULK_SIZE / 4; ++i){
			write_serial(d, c, BULK_SIZE * 5);
		}
	}else if(mode = 3){
		// similar to mode 2 but uses a single CMD_BW instead of a series of CMD_W
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

int x(tDev d, int mode, u32 v){
	int i;
	u32 r;
	u8 c;
	switch(mode){
		case 1:
			c = CMD_XFER | CMD_FLAG_W | CMD_FLAG_R;
			write_serial(d, &c, 1);
			write_serial(d, &v, 4);
			read_serial(d, &r, 4);
			fprintf(stderr, "%08x <-> %08x\n", v, r);
			break;
		case 2:
			c = CMD_XFER | CMD_FLAG_W | CMD_FLAG_R | CMD_FLAG_B;
			write_serial(d, &c, 1);
			for(i = 0; i < BULK_SIZE; ++i){
				write_serial(d, &v, 4);
			}
			for(i = 0; i < BULK_SIZE; ++i){
				read_serial(d, &r, 4);
				fprintf(stderr, "%08x <-> %08x\n", v, r);
			}
			break;
	}
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
	if(argc < 2){
		fprintf(stderr, "you should at least specify a serial device name");
		return -1;
	}

	d = open_serial(argv[1]);
	if(validate_serial(d)){
		fprintf(stderr, "can't open %s\n", argv[1]);
		return -1;
	}
	fprintf(stderr, "open %s success\n", argv[1]);

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
	}else if(argc == 5 && !strcmp(argv[2], "x")){
		return x(d, atoi(argv[3]), strtoul(argv[4], NULL, 0x10));
	}else{
		fprintf(stderr, "invalid parameters, example:\n\t%s COM1 multiboot your_file.gba\n", argv[0]);
		return -1;
	}
}

