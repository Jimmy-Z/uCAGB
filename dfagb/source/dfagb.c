
#include <gba_console.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <gba_sio.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../common/common.h"
#include "../../common/crc32.h"

const char sTitle[] = "DFAGB - Dumper/Flasher for GBA build %s %s\n";

void irq_keypad(void){
	SystemCall(0x26);
}

// 128K bytes buffer
EWRAM_BSS vu8 buf[AGB_BUF_SIZE];
#define buf16 ((vu16*)buf)
#define buf32 ((vu32*)buf)
u32 crc32_table[CRC32_TABLE_LEN];

// FSM stat and parameters
#define FSM_IDLE	0
#define FSM_UPLOADING	1
#define FSM_DOWNLOADING	2
#define FSM_READING	3
#define FSM_WORKER	0x10
// if I don't declare this as volatile, worker never wakes up
// some ridiculous compiler stunts?
vu32 fsm_state, fsm_p0, fsm_p1, fsm_p3, fsm_p4;

void start_serial(u32 out32){
	REG_SIODATA32 = out32;
	REG_SIOCNT &= ~(SIO_SO_HIGH);
	REG_SIOCNT |= SIO_START;
	REG_SIOCNT |= SIO_SO_HIGH;
}

/*
upload 4*u32 example
===
	s == idle, out == idle
01000004	<->	<IDLE>
	s = uploading, s0 = 4, p1 = 0
<payload0>	<->	<undefined>
	buf[0] = payload0, p1 = 1
<payload1>	<->	<undefined>
	buf[1] = payload1, p1 = 2
<payload2>	<->	<undefined>
	buf[2] = payload2, p1 = 3
<payload3>	<->	<undefined>
	buf[3] = payload3, p1 = 4, s = idle, out = idle
<nop>		<->	<IDLE>

download 4*u32 example
===
	s == idle, out == idle
02000004	<->	<IDLE>
	s = downloading, s0 = 4, p1 = 0, out = buf[0], p1 = 1
<any>		<->	buf[0]
	out = buf[1], p1 = 2
<any>		<->	buf[1]
	out = buf[2], p1 = 3
<any>		<->	buf[2]
	out = buf[3], p1 = 4
	you might argue that we can set state to idle now
	but then the next input will be handled in idle mode
	thus treated as a new command
<any>		<->	buf[3]
	s = idle, out = idle
<nop>		<-> 	<IDLE>

worker example
===
	s == idle, out == idle
10020000	<->	<IDLE>
	p0 = cmd, out = <BUSY>, s = worker
<nop>		<->	<BUSY>
...
// util worker in the main thread set s = IDLE
<nop>		<->	<BUSY>
	out = idle
<nop>		<->	<IDLE>

*/
IWRAM_CODE void irq_serial(void){
	u32 in32 = REG_SIODATA32, out32 = DF_STATE_IDLE;
	switch(fsm_state){
		case FSM_IDLE:
			switch(in32 & DF_CMD_MASK){
				case DF_CMD_UPLOAD:
					// upload to buf
					fsm_state = FSM_UPLOADING;
					// length, of u32
					fsm_p0 = in32 & DF_PARAM_MASK;
					// TODO: we should complain about size exceeds buffer length
					iprintf("\nreceiving %d bytes", fsm_p0 << 2);
					// current offset
					fsm_p1 = 0;
					// during upload, the PC side doesn't care about data they receive
					break;
				case DF_CMD_DOWNLOAD:
					// download to buf
					fsm_state = FSM_DOWNLOADING;
					// length, of u32
					fsm_p0 = in32 & DF_PARAM_MASK;
					// TODO: we should complain about size exceeds buffer length
					iprintf("\nsending %d bytes", fsm_p0 << 2);
					// current offset
					fsm_p1 = 0;
					// PC is expecting data on the next return
					out32 = buf32[fsm_p1++];
					break;
				case DF_CMD_READ:
					// read p0
					out32 = fsm_p0;
					fsm_state = FSM_READING;
					break;
				case DF_CMD_CRC32:
				case DF_CMD_READ_SRAM:
				case DF_CMD_WRITE_SRAM:
				case DF_CMD_READ_FLASH:
				case DF_CMD_WRITE_FLASH:
				case DF_CMD_READ_EEPROM:
				case DF_CMD_WRITE_EEPROM:
				case DF_CMD_DUMP:
				case DF_CMD_ID:
				case DF_CMD_UNLOCK:
				case DF_CMD_ERASE:
				case DF_CMD_PROGRAM:
					fsm_p0 = in32;
					out32 = DF_STATE_BUSY;
					fsm_state = FSM_WORKER;
					// iprintf("\nworker command: 0x%08x", in32);
					break;
				default:
					if(in32 == DF_CMD_NOP){
					}else if(in32 == MULTIBOOT_PING){
						SystemCall(0x26);
					}else{
						iprintf("\ninvalid command 0x%08x", in32);
					}
					break;
			}
			break;
		case FSM_UPLOADING:
			buf32[fsm_p1++] = in32;
			if(fsm_p1 >= fsm_p0){
				fsm_state = FSM_IDLE;
				iprintf(", done");
			}
			break;
		case FSM_DOWNLOADING:
			if(fsm_p1 < fsm_p0){
				out32 = buf32[fsm_p1++];
			}else{
				fsm_state = FSM_IDLE;
				iprintf(", done");
			}
			break;
		case FSM_READING:
			fsm_state = FSM_IDLE;
			break;
		case FSM_WORKER:
			out32 = DF_STATE_BUSY;
			break;
	}
	start_serial(out32);
}

#define CART_BASE 0x08000000 // ends @ 0x09ffffff

// based on Intel 28FxxxJ3D datasheet

#define I28F_WB_SIZE	0x10 // 32 bytes / 16 words write buffer

// 1st class
#define I28F_RA		0xff // Read Array
#define I28F_RIC	0x90 // Read Identifier Codes
#define I28F_RSR	0x70 // Read Status Register
#define I28F_CSR	0x50 // Clear Status Register
#define I28F_WB		0xE8 // Write to Buffer
#define I28F_BE		0x20 // Block Erase
#define I28F_BLB	0x60 // Set/Clear Block Lock-Bits
// 2nd class
#define I28F_CONFIRM	0xD0 // confirm

#define I28F_WSMS_READY	0x80 // Write State Machine Status, SR7, 1 = Ready

// the volatile declaration is mandatory for FLASH operation
#define WRITE8(_ADDR, _V)	*(vu8*)(_ADDR) = (_V)
#define READ8(_ADDR)		(*(vu8*)(_ADDR))
#define WRITE16(_ADDR, _V)	*(vu16*)(_ADDR) = (_V)
#define READ16(_ADDR)		(*(vu16*)(_ADDR))
#define READ16(_ADDR)		(*(vu16*)(_ADDR))
#define WRITE32(_ADDR, _V)	*(vu32*)(_ADDR) = (_V)
#define READ32(_ADDR)		(*(vu32*)(_ADDR))

u32 cart_id(u32 offset){
	// since we have only 24 bit parameter space
	// and the offset should be able to cover the entire ROM length 0x02000000
	// the offset is shifted 8 bits
	offset = CART_BASE + (offset << 8);
	WRITE16(offset, I28F_RIC);
	u8 m = READ16(offset), d = READ16(offset + 2);
	iprintf("\nManufacture/Device: %02x, %02x", m, d);
	if(m == 0x89){
		u16 s;
		if(d == 0x1d){
			s = 256;
		}else{
			s = 1 << (d - 0x11);
		}
		iprintf("\nIntel %dM", s);
	}else if(m == 0x2e){
		iprintf("\nnot a Flash cart");
	}else{
		iprintf("\nFlash type not supported");
	}
	WRITE16(offset, I28F_RA);
	return (m << 16) | d;
}

u32 cart_wait_wsms(u32 offset, u16 command){
	while(1){
		if(command){
			WRITE16(offset, command);
		}
		u32 sr = READ16(offset);
		if(sr & I28F_WSMS_READY){
			return sr;
		}else{
			// TODO: a better wait
			asm("nop");
		}
	}
}

u32 cart_unlock(u32 offset){
	// unlock is chip wise and erase is block wise, so they are separated
	offset = CART_BASE + (offset << 8);
	// TODO: if no block is locked...
	iprintf("\nunlocking 0x%08x", offset);
	WRITE16(offset, I28F_BLB);
	WRITE16(offset, I28F_CONFIRM);
	u32 sr = cart_wait_wsms(offset, 0);
	if(sr == I28F_WSMS_READY){
		iprintf(", done");
	}else{
		iprintf("\n! failed, SR = 0x%02x", sr);
		WRITE16(offset, I28F_CSR);
	}
	WRITE16(offset, I28F_RA);
	return sr;
}

u32 cart_erase(u32 offset){
	offset = CART_BASE + (offset << 8);
	iprintf("\nerasing 0x%08x", offset);
	WRITE16(offset, I28F_BE);
	WRITE16(offset, I28F_CONFIRM);
	u32 sr = cart_wait_wsms(offset, 0);
	if(sr == I28F_WSMS_READY){
		iprintf(", done");
	}else{
		iprintf("\n! failed, SR = 0x%02x", sr);
		WRITE16(offset, I28F_CSR);
	}
	WRITE16(offset, I28F_RA);
	return sr;
}

u32 cart_program(u32 offset){
	u32 o1, o2, sr;
	offset = CART_BASE + (offset << 8);
	iprintf("\nprogramming 0x%08x", offset);
	// caution these are all 16 bit wise operations
	for(o1 = 0; o1 < (AGB_BUF_SIZE >> 1); o1 += I28F_WB_SIZE){
		cart_wait_wsms(offset + (o1 << 1), I28F_WB);
		WRITE16(offset + (o1 << 1), I28F_WB_SIZE - 1);
		for(o2 = 0; o2 < I28F_WB_SIZE; ++o2){
			WRITE16(offset + ((o1 + o2) << 1), buf16[o1 + o2]);
		}
		WRITE16(offset + (o1 << 1), I28F_CONFIRM);
		sr = cart_wait_wsms(offset + (o1 << 1), I28F_RSR);
		if(sr != I28F_WSMS_READY){
			break;
		}
	}
	if(sr == I28F_WSMS_READY){
		iprintf(", done");
	}else{
		iprintf("\n! failed, SR = 0x%02x", sr);
		WRITE16(offset, I28F_CSR);
	}
	WRITE16(offset, I28F_RA);
	return sr;
}

void cart_dump(u32 offset){
	u32 o1;
	offset = CART_BASE + (offset << 8);
	iprintf("\ndumping 0x%08x", offset);
	for(o1 = 0; o1 < (AGB_BUF_SIZE >> 1); ++o1){
		buf16[o1] = READ16(offset + (o1 << 1));
	}
	iprintf(", done");
}

void read_sram(u32 length){
	// SRAM can only be accessed 8 bit wise
	u32 i;
	iprintf("\nreading SRAM %dK", length >> 10);
	for(i = 0; i < length; ++i){
		buf[i] = READ8(SRAM + i);
	}
	iprintf(", done");
}

void write_sram(u32 length){
	u32 i;
	iprintf("\nwriting SRAM %dK", length >> 10);
	for(i = 0; i < length; ++i){
		WRITE8(SRAM + i, buf[i]);
	}
	iprintf(", done");
}

void worker(void){
	switch(fsm_p0 & DF_CMD_MASK){
		case DF_CMD_CRC32:
			iprintf("\nCRC32(0x%06x)", fsm_p0 & DF_PARAM_MASK);
			fsm_p0 = crc32(crc32_table, 0, (const void *)buf, fsm_p0 & DF_PARAM_MASK);
			iprintf(": 0x%08x", fsm_p0);
			break;
		case DF_CMD_ID:
			fsm_p0 = cart_id(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_UNLOCK:
			fsm_p0 = cart_unlock(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_ERASE:
			fsm_p0 = cart_erase(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_PROGRAM:
			fsm_p0 = cart_program(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_DUMP:
			cart_dump(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_READ_SRAM:
			read_sram(fsm_p0 & DF_PARAM_MASK);
			break;
		case DF_CMD_WRITE_SRAM:
			write_sram(fsm_p0 & DF_PARAM_MASK);
			break;
		default:
			iprintf("\ninvalid worker command: 0x%08x", fsm_p0);
	}
	fsm_state = FSM_IDLE;
}

int main(void) {
	// keypad setup
	REG_KEYCNT = KEY_SELECT | KEY_START | KEYIRQ_ENABLE | KEYIRQ_AND;

	// serial setup
	REG_RCNT = R_NORMAL;
	REG_SIOCNT = SIO_32BIT | SIO_IRQ;

	// irq setup
	irqInit();
	irqSet(IRQ_KEYPAD, irq_keypad);
	irqSet(IRQ_SERIAL, irq_serial);
	irqEnable(IRQ_VBLANK | IRQ_KEYPAD | IRQ_SERIAL);

	fsm_state = FSM_IDLE;
	start_serial(DF_STATE_IDLE);

	// console setup, see libdgba/src/console.c for details
	consoleInit(0, 4, 0, NULL, 0, 15);
	BG_COLORS[0] = RGB8(0, 0, 0);
	BG_COLORS[241] = RGB5(31, 31, 31);
	SetMode(MODE_0 | BG0_ON);

	iprintf(sTitle, __DATE__, __TIME__);

	//iprintf("\n%dKB buffer @ 0x%08x", AGB_BUF_SIZE >> 10, (u32)buf);

	init_crc32_table(crc32_table);
	iprintf("\nCRC32 table @ 0x%08x", (u32)crc32_table);

	while (1) {
		// TODO: a shorter wait
		VBlankIntrWait();
		if(fsm_state == FSM_WORKER){
			worker();
		}
	}

}


