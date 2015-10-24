
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

// 128K bytes buffer, thats
#define BUF_SIZE 0x20000
EWRAM_BSS u8 buf[BUF_SIZE];
#define buf16 ((u16*)buf)
#define buf32 ((u32*)buf)
u32 crc32_table[CRC32_TABLE_LEN];

// SIO FSM stat and parameters
#define SIO_IDLE 0
#define SIO_UPLOAD 1
#define SIO_DOWNLOAD 2
u32 sio_state, sio_p0, sio_p1, sio_p3, sio_p4;

void start_serial(u32 out32){
	REG_SIODATA32 = out32;
	REG_SIOCNT &= ~(SIO_SO_HIGH);
	REG_SIOCNT |= SIO_START;
	REG_SIOCNT |= SIO_SO_HIGH;
}

/*
upload 4 example
===
	s == idle, out == idle
00010004	<->	<IDLE>
	s = upload, s0 = 4, p1 = 0
<payload0>	<->	<undefined>
	buf[0] = payload0, p1 = 1
<payload1>	<->	<undefined>
	buf[1] = payload1, p1 = 2
<payload2>	<->	<undefined>
	buf[2] = payload2, p1 = 3
<payload3>	<->	<undefined>
	buf[3] = payload3, p1 = 4, s = idle, out = idle
<nop>		<->	<IDLE>

download 4 example
===
	s == idle, out == idle
00020004	<->	<IDLE>
	s = download, s0 = 4, p1 = 0, out = buf[0], p1 = 1
<undefined>	<->	buf[0]
	out = buf[1], p1 = 2
<undefined>	<->	buf[1]
	out = buf[2], p1 = 3
<undefined>	<->	buf[2]
	out = buf[3], p1 = 4
<undefined>	<->	buf[3]
	s = idle, out = idle
<nop>		<-> 	<IDLE>
*/
void irq_serial(void){
	u32 in32 = REG_SIODATA32, out32 = DF_STATE_IDLE;
	switch(sio_state){
		case SIO_IDLE:
			switch(in32 & 0xff000000){
				case DF_CMD_UPLOAD:
					// upload to buf
					sio_state = SIO_UPLOAD;
					// length, of u32
					sio_p0 = in32 & 0x0000ffff;
					iprintf("\nstart uploading %d bytes", sio_p0 << 2);
					// current offset
					sio_p1 = 0;
					// during upload, the PC side doesn't care about data they receive
					break;
				case DF_CMD_DOWNLOAD:
					// download to buf
					sio_state = SIO_DOWNLOAD;
					// length, of u32
					sio_p0 = in32 & 0x0000ffff;
					iprintf("\nstart downloading %d bytes", sio_p0 << 2);
					// current offset
					sio_p1 = 0;
					// PC is expecting data on the next return
					out32 = buf32[sio_p1++];
					break;
				// case DF_CMD_FLASH:
				// case DF_CMD_NOP & 0xff000000:
				default:
					if(in32 != DF_CMD_NOP){
						iprintf("\ninvalid command: 0x%08x", in32);
					}
					break;
			}
			break;
		case SIO_UPLOAD:
			buf32[sio_p1++] = in32;
			if(sio_p1 >= sio_p0){
				sio_state = SIO_IDLE;
				iprintf("\nupload complete");
			}
			break;
		case SIO_DOWNLOAD:
			if(sio_p1 < sio_p0){
				out32 = buf32[sio_p1++];
			}else{
				sio_state = SIO_IDLE;
				iprintf("\ndownload complete");
			}
			break;
	}
	start_serial(out32);
}

// WORKER FSM and states

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

	sio_state = SIO_IDLE;
	start_serial(DF_STATE_IDLE);

	// console setup, see libdgba/src/console.c for details
	consoleInit(0, 4, 0, NULL, 0, 15);
	BG_COLORS[0] = RGB8(0, 0, 0);
	BG_COLORS[241] = RGB5(31, 31, 31);
	SetMode(MODE_0 | BG0_ON);

	iprintf(sTitle, __DATE__, __TIME__);

	iprintf("\n%dKB buffer @ 0x%08x", BUF_SIZE >> 10, (u32)buf);

	init_crc32_table(crc32_table);
	iprintf("\nCRC32 table @ 0x%08x", (u32)crc32_table);

	while (1) {
		VBlankIntrWait();
	}

}


