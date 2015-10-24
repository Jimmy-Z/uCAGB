
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
EWRAM_BSS u8 buf[AGB_BUF_SIZE];
#define buf16 ((u16*)buf)
#define buf32 ((u32*)buf)
u32 crc32_table[CRC32_TABLE_LEN];

// FSM stat and parameters
#define FSM_IDLE	0
#define FSM_UPLOADING	1
#define FSM_DOWNLOADING	2
#define FSM_READING	3
#define FSM_WORKER	0x10
// if I don't declare this as volatile, worker never wakes up, why?
volatile u32 fsm_state, fsm_p0, fsm_p1, fsm_p3, fsm_p4;

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
<undefined>	<->	buf[0]
	out = buf[1], p1 = 2
<undefined>	<->	buf[1]
	out = buf[2], p1 = 3
<undefined>	<->	buf[2]
	out = buf[3], p1 = 4
	you might argue that we can set state to idle now
	but then the next input will be treated as a new command
<undefined>	<->	buf[3]
	s = idle, out = idle
<nop>		<-> 	<IDLE>

worker example
===
	s == idle, out == idle

*/
void irq_serial(void){
	u32 in32 = REG_SIODATA32, out32 = DF_STATE_IDLE;
	switch(fsm_state){
		case FSM_IDLE:
			switch(in32 & 0xff000000){
				case DF_CMD_UPLOAD:
					// upload to buf
					fsm_state = FSM_UPLOADING;
					// length, of u32
					fsm_p0 = in32 & 0x00ffffff;
					// TODO: we should complain about size exceeds buffer length
					iprintf("\nuploading %d bytes", fsm_p0 << 2);
					// current offset
					fsm_p1 = 0;
					// during upload, the PC side doesn't care about data they receive
					break;
				case DF_CMD_DOWNLOAD:
					// download to buf
					fsm_state = FSM_DOWNLOADING;
					// length, of u32
					fsm_p0 = in32 & 0x00ffffff;
					// TODO: we should complain about size exceeds buffer length
					iprintf("\ndownloading %d bytes", fsm_p0 << 2);
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
				case DF_CMD_FLASH:
				case DF_CMD_DUMP:
				case DF_CMD_READ_SRAM:
				case DF_CMD_WRITE_SRAM:
				case DF_CMD_READ_FLASH:
				case DF_CMD_WRITE_FLASH:
				case DF_CMD_READ_EEPROM:
				case DF_CMD_WRITE_EEPROM:
					fsm_p0 = in32;
					out32 = DF_STATE_BUSY;
					fsm_state = FSM_WORKER;
					// iprintf("\nworker command: 0x%08x", in32);
					break;
				// case DF_CMD_NOP & 0xff000000:
				default:
					if(in32 != DF_CMD_NOP){
						iprintf("\ninvalid command");
					}
					break;
			}
			break;
		case FSM_UPLOADING:
			buf32[fsm_p1++] = in32;
			if(fsm_p1 >= fsm_p0){
				fsm_state = FSM_IDLE;
				iprintf("\nupload complete");
			}
			break;
		case FSM_DOWNLOADING:
			if(fsm_p1 < fsm_p0){
				out32 = buf32[fsm_p1++];
			}else{
				fsm_state = FSM_IDLE;
				iprintf("\ndownload complete");
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

void worker(void){
	switch(fsm_p0 & 0xff000000){
		case DF_CMD_CRC32:
			iprintf("\nCRC32(0x%06x):", fsm_p0 & 0x00ffffff);
			fsm_p0 = crc32(crc32_table, 0, buf, fsm_p0 & 0x00ffffff);
			iprintf(" 0x%08x", fsm_p0);
			break;
		default:
			iprintf("\ninvalid worker command");
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


