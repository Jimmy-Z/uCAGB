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
		fprintf(stderr, "failed to open \"%s\" for read\n", filename);
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
		fprintf(stderr, "failed to load %s\n", filename);
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

void df_wait(tDev d, const char *msg){
	u32 r;
	do{
		r = xfer32(d, DF_CMD_NOP);
		sleep(1000/0x10);
		fprintf(stderr, "\r%s, response: 0x%08x", msg, r);
	}while(r != DF_STATE_IDLE);
}

void df_upload(tDev d, const void * buf, u32 size){
	unsigned t;
	// df_wait(d, "waiting for DFAGB");
	fprintf(stderr, "uploading %d bytes to DFAGB...\n", size);
	t = get_rtime();
	xfer32wo(d, DF_CMD_UPLOAD | (size >> 2));
	xfer32bw(d, buf, size);
	t = get_rtime() - t;
	fprintf(stderr, "upload to DFAGB complete, %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		t / 1000.0, size * 8.0 / t, size * 1.0 / t);
}

void df_download(tDev d, void *buf, u32 size){
	unsigned t;
	fprintf(stderr, "downloading %d bytes from DFAGB...\n", size);
	t = get_rtime();
	xfer32wo(d, DF_CMD_DOWNLOAD | (size >> 2));
	xfer32br(d, buf, size);
	t = get_rtime() - t;
	fprintf(stderr, "download from DFAGB complete, %.2f seconds, average speed %.2f Kbps(%.2f KB/s)\n",
		t / 1000.0, size * 8.0 / t, size * 1.0 / t);
}

u32 df_worker(tDev d, u32 cmd, const char *msg0, const char *msg1, const char *msg2){
	u32 t, r;
	if(msg0){
		fprintf(stderr, msg0);
	}
	t = get_rtime();
	xfer32wo(d, cmd);
	df_wait(d, msg1);
	xfer32wo(d, DF_CMD_READ);
	r = xfer32ro(d);
	t = get_rtime() - t;
	fprintf(stderr, "\n%s, response: 0x%08x, %.2f seconds\n",
		msg2, r, t / 1000.0);
	return r;
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

	df_upload(d, buf, AGB_BUF_SIZE);

	while(1){
		crc = df_worker(d, DF_CMD_CRC32 | AGB_BUF_SIZE,
			NULL, "waiting for DFAGB CRC32", "DFAGB CRC32 returned");
		if(crc == 0x454c4449){
			break;
		}
	}

	df_download(d, buf, AGB_BUF_SIZE);

	// save_file("128K.b.bin", buf, AGB_BUF_SIZE);
	crc = crc32(crc32_table, 0, buf, AGB_BUF_SIZE);
	fprintf(stderr, "buffer CRC32: 0x%08x\n", crc);

	return 0;
}

int df_flash(tDev d, const char *filename, u32 start){
	u8 *rom;
	u32 r, size, i, total, crc0, crc1;

	set_wait(d, 1, 0);

	r = df_worker(d, DF_CMD_ID,
		NULL, "waiting for Flash ID", "Flash ID returned");
	if (r != 0x00890018){
		fprintf(stderr, "sorry, unsupported flash\n");
		return -1;
	}

	rom = load_file(filename, &size, AGB_BUF_SIZE);
	if(rom == NULL){
		return -1;
	}
	// fprintf(stderr, "rom loaded @%08x\n", (u32)rom);

	r = df_worker(d, DF_CMD_UNLOCK,
		NULL, "waiting for clearing Block-Lock Bits", "done");
	if(r != 0x80){
		return -1;
	}

	total = size / AGB_BUF_SIZE;
	fprintf(stderr, "needs %d upload/erase/program cycles\n", total);

	if (start < 1 || start > total){
		start = 1;
	}
	for(i = start - 1; i < total; ++i){
		fprintf(stderr, " === %d / %d ===\n", i + 1, total);
		// coincidentally our AGB_BUF_SIZE == I28F128J3 block size
		// fprintf(stderr, "processing rom block %d @%08x\n", i, (u32)(rom + i * AGB_BUF_SIZE));
		crc0 = crc32(crc32_table, 0, rom + i * AGB_BUF_SIZE, AGB_BUF_SIZE);

		// TODO: dump and compare to skip identical blocks

		// some ugly retry
		while(1){
			df_upload(d, rom + i * AGB_BUF_SIZE, AGB_BUF_SIZE);
			crc1 = df_worker(d, DF_CMD_CRC32 | AGB_BUF_SIZE,
				NULL, "waiting for DFAGB CRC32", "DFAGB CRC32 returned");
			if(crc0 == crc1){
				fprintf(stderr, "CRC match, 0x%08x == 0x%08x\n", crc0, crc1);
				break;
			}else{
				fprintf(stderr, "CRC mismatch, 0x%08x != 0x%08x\n", crc0, crc1);
			}
		}
		if(!df_worker(d, DF_CMD_VERIFY | (i * AGB_BUF_SIZE >> 8),
			NULL, "verifying", "done")){
			fprintf(stderr, "identical block, skipped\n");
			continue;
		}
		while(1){
			r = df_worker(d, DF_CMD_ERASE | (i * AGB_BUF_SIZE >> 8),
				NULL, "erasing", "done");
			if(r != 0x80){
				continue;
			}
			// I've seen r = DF_STATE_IDLE instead of 0x80 while GBA side is OK
			// very hard to reproduce, I can't figure out why :(
			r = df_worker(d, DF_CMD_PROGRAM | (i * AGB_BUF_SIZE >> 8),
				NULL, "programming", "done");
			if(r != 0x80){
				continue;
			}
			break;
			// TODO: verify the block
		}
	}

	// TODO: lock blocks
	return 0;
}

int df_dump(tDev d, u32 size, const char *filename){
	u32 i, total, crc0, crc1;
	u8 *buf;

	size <<= 17; // input Mbits
	total = size / AGB_BUF_SIZE;
	buf = malloc(size);

	set_wait(d, 1, 0);

	for(i = 0; i < total; ++ i){
		fprintf(stderr, " === %d / %d ===\n", i + 1, total);
		df_worker(d, DF_CMD_DUMP | (i * AGB_BUF_SIZE >> 8),
			NULL, "waiting for dump", "done");
		crc0 = df_worker(d, DF_CMD_CRC32 | AGB_BUF_SIZE,
			NULL, "waiting for DFAGB CRC32", "DFAGB CRC32 returned");

		df_download(d, buf + i * AGB_BUF_SIZE, AGB_BUF_SIZE);
		crc1 = crc32(crc32_table, 0, buf + i * AGB_BUF_SIZE, AGB_BUF_SIZE);
		if(crc0 == crc1){
			fprintf(stderr, "CRC match, 0x%08x == 0x%08x\n", crc0, crc1);
		}else{
			fprintf(stderr, "CRC mismatch, 0x%08x != 0x%08x\n", crc0, crc1);
			return -1;
		}
	}

	save_file(filename, buf, size);

	return 0;
}

void parse_save_type(const char *save_type, int is_write, u32 *p_cmd, u32 *p_size){
	if(strcmp(save_type, "sram256") || strcmp(save_type, "sram32")){
		*p_cmd = is_write ? DF_CMD_WRITE_SRAM : DF_CMD_READ_SRAM;
		*p_size = 0x8000;
	}else if(strcmp(save_type, "sram512") || strcmp(save_type, "sram64")){
		*p_cmd = is_write ? DF_CMD_WRITE_SRAM : DF_CMD_READ_SRAM;
		*p_size = 0x10000;
	}else if(strcmp(save_type, "eeprom64") || strcmp(save_type, "eeprom8")){
		*p_cmd = is_write ? DF_CMD_WRITE_EEPROM : DF_CMD_READ_EEPROM;
		*p_size = 0x2000;
	}else if(strcmp(save_type, "eeprom4") || strcmp(save_type, "eeprom0.5") || strcmp(save_type, "eeprom512")){
		*p_cmd = is_write ? DF_CMD_WRITE_EEPROM : DF_CMD_READ_EEPROM;
		*p_size = 0x200;
	}else{
		fprintf(stderr, "invalid save type: %s\n", save_type);
		*p_size = 0;
	}
}

// write save file to cart
int df_write(tDev d, const char *save_type, const char *filename){
	u32 cmd, size, fsize, crc0, crc1;
	u8 *p_save;
	parse_save_type(save_type, 1, &cmd, &size);
	if(size == 0){
		return -1;
	}
	p_save = load_file(filename, &fsize, size);
	if(p_save == NULL){
		return -1;
	}
	crc0 = crc32(crc32_table, 0, p_save, size);

	set_wait(d, 1, 0);

	df_upload(d, p_save, size);
	crc1 = df_worker(d, DF_CMD_CRC32 | size,
		NULL, "waiting for DFAGB CRC32", "done");

	if(crc0 == crc1){
		fprintf(stderr, "CRC match, 0x%08x == 0x%08x\n", crc0, crc1);
	}else{
		fprintf(stderr, "CRC mismatch, 0x%08x != 0x%08x\n", crc0, crc1);
		return -1;
	}

	df_worker(d, cmd | size, NULL, "waiting for write save", "done");

	return 0;
}

// read save from cart and write to file
int df_read(tDev d, const char *save_type, const char *filename){
	u32 cmd, size, crc0, crc1;
	u8 *p_save;
	parse_save_type(save_type, 0, &cmd, &size);
	if(size == 0){
		return -1;
	}
	p_save = malloc(size);

	set_wait(d, 1, 0);
	df_worker(d, cmd | size,
		NULL, "waiting for read save", "done");
	crc0 = df_worker(d, DF_CMD_CRC32 | size,
		NULL, "waiting for DFAGB CRC32", "done");
	df_download(d, p_save, size);
	crc1 = crc32(crc32_table, 0, p_save, size);
	if(crc0 == crc1){
		fprintf(stderr, "CRC match, 0x%08x == 0x%08x\n", crc0, crc1);
	}else{
		fprintf(stderr, "CRC mismatch, 0x%08x != 0x%08x\n", crc0, crc1);
		return -1;
	}
	save_file(filename, p_save, size);

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

	setup_serial(d);

	if(validate_uC(d)){
		fprintf(stderr, "ping %s failed\n", argv[1]);
		return -1;
	}
	fprintf(stderr, "ping %s success\n", argv[1]);

	if(argc == 4 && !strcmp(argv[2], "multiboot")){
		// example: usbagb com3 multiboot game.gba
		return multiboot(d, argv[3]);
	}else if(argc == 4 && !strcmp(argv[2], "flash")){
		// example: usbagb com3 flash game.gba
		return df_flash(d, argv[3], 1);
	}else if(argc == 5 && !strcmp(argv[2], "flash")){
		// continue flash starting at specified block
		// example: usbagb com3 flash game.gba 4
		return df_flash(d, argv[3], atoi(argv[4]));
	}else if(argc == 5 && !strcmp(argv[2], "dump")){
		// example: usbagb com3 dump 128 dump.gba
		return df_dump(d, atoi(argv[3]), argv[4]);
	}else if(argc == 5 && !strcmp(argv[2], "write")){
		// example: usbagb com3 write sram256 game.sav
		return df_write(d, argv[3], argv[4]);
	}else if(argc == 5 && !strcmp(argv[2], "read")){
		// example: usbagb com3 read sram256 game.sav
		return df_read(d, argv[3], argv[4]);
	}else if(argc == 3 && !strcmp(argv[2], "bootloader")){
		// reset the uCSIO to bootloader
		// example: usbagb com3 bootloader
		return reset_to_bootloader(d);
	}else if(argc == 5 && !strcmp(argv[2], "test")){
		return serial_bench(d, atoi(argv[3]), atoi(argv[4]));
	}else if(argc == 5 && !strcmp(argv[2], "testdf")){
		return df_test(d, atoi(argv[3]), strtoul(argv[4], NULL, 0x10));
	}else{
		fprintf(stderr, "invalid parameters, example:\n\t%s COM1 multiboot your_file.gba\n", argv[0]);
		return -1;
	}
}

