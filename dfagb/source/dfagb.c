
#include <gba_console.h>
#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <gba_sio.h>
#include <stdio.h>
#include <stdlib.h>

const char sTitle[] = "DFAGB - Dumper/Flasher for GBA build %s %s\n";

void irq_keypad(void){
	SystemCall(0x26);
}

// serial processings
u32 so_data = 0;
u32 si_data;

void start_serial(){
	REG_SIODATA32 = so_data;
	REG_SIOCNT |= SIO_START;
}

void irq_serial(void){
	si_data = REG_SIODATA32;
	iprintf("\nSIODATA32: 0x%08x", si_data);
	start_serial();
}

#define BUF_SIZE 0x20000
EWRAM_BSS u8 buf[BUF_SIZE];

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

	start_serial();

	// console setup, see libdgba/src/console.c for details
	consoleInit(0, 4, 0, NULL, 0, 15);
	BG_COLORS[0] = RGB8(0, 0, 0);
	BG_COLORS[241] = RGB5(31, 31, 31);
	SetMode(MODE_0 | BG0_ON);

	iprintf(sTitle, __DATE__, __TIME__);

	iprintf("\n%dKB buffer @ 0x%08x", BUF_SIZE >> 10, (u32)buf);

	while (1) {
		VBlankIntrWait();
	}

}


