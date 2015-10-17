#include <stdio.h>
#include <stdlib.h>

#include "pl_win.h"
#include "../common/cmd.h"
#include "gba.h"

static const tSize align = 0x10;
const unsigned char *load_rom(const char *filename, tSize *psize){
	tSize size;
	FILE *f;
	unsigned char *data;
	f = fopen(filename, "rb");
	if(!f){
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	*psize = (size + align - 1) & (~(align - 1));
	data = malloc(*psize);
	memset(data, 0, *psize);
	fseek(f, 0, SEEK_SET);
	fread(data, 1, size, f);
	fclose(f);
	return data;
}

int multiboot(tDev d, const char *filename){
	const char *rom;
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
	fprintf(stderr, "transfer time: %.2f seconds, average speed %.2f Kbps\n",
		dt / 1000.0, size * 8 / dt * 1.0);

	return 0;
}


void reset_to_bootloader(tDev d){
	u8 c = CMD_BOOTLOADER;
	write_serial(d, &c, 1);
	return;
}

// my test results show that this is just a tiny bit faster than multiboot
int test0(tDev d, int length){
	uint i, t0, dt;
	u8 c[5];

	t0 = get_rtime();
	c[0] = CMD_FLAG_W;
	fprintf(stderr, "starting %d * 4 = %d bytes serial speed test\n", length, length * 4);
	for(i = 0; i < length; ++i){
		write_serial(d, c, 5);
	}

	dt = get_rtime() - t0;
	fprintf(stderr, "transfer time: %.2f seconds, average speed %.2f Kbps\n",
		dt / 1000.0, length * 4 * 8 / dt * 1.0);

	return 0;
}

#define TEST_PATTERN 0xff00aa55
int validate_uC(tDev d){
	u8 c[5];
	c[0] = CMD_PING;
	*((u32*)&c[1]) = TEST_PATTERN;
	write_serial(d, c, 5);
	read_serial(d, c, 4);
	// fprintf(stderr, "ping returned 0x%08x, expecting 0x%08x\n", *((u32*)c), ~(u32)TEST_PATTERN);
	return (*((u32*)c)) ^ (~(u32)TEST_PATTERN);
}

int main(int argc, const char *argv[]){
	tDev siodev;
	if(argc < 2){
		fprintf(stderr, "you should at least specify a serial device name");
		return -1;
	}

	siodev = open_serial(argv[1]);
	if(validate_serial(siodev)){
		fprintf(stderr, "can't open %s\n", argv[1]);
		return -1;
	}

	if(validate_uC(siodev)){
		fprintf(stderr, "ping %s failed\n", argv[1]);
		return -1;
	}
	fprintf(stderr, "ping %s success\n", argv[1]);

	if(argc == 4 && !strcmp(argv[2], "multiboot")){
		return multiboot(siodev, argv[3]);
	}else if(argc == 3 && !strcmp(argv[2], "bootloader")){
		return reset_to_bootloader(siodev);
	}else if(argc == 4 && !strcmp(argv[2], "test0")){
		return test0(siodev, atoi(argv[3]));
	}else{
		fprintf(stderr, "invalid parameters, example:\n\t%s COM1 multiboot your_file.gba\n", argv[0]);
		return -1;
	}
}

