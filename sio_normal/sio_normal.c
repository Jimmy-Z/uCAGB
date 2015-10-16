/*
This file is part of DFAGB
DFAGB is forked from the USB-GBA multiboot project
https://github.com/tangrs/usb-gba-multiboot/

DFAGB is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DFAGB is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with DFAGB.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <avr/interrupt.h>
#include <avr/power.h>
#include <util/delay.h>

#include "usb_serial.h"

#include "../common/cmd.h"

#define GBA_DDR DDRB
#define GBA_OUT PORTB
#define GBA_IN PINB
#define MOSI_BIT 2
#define MISO_BIT 3
#define CLK_BIT 1

inline uint32_t xfer(uint32_t data) {
	unsigned int i;
	cli();

	for (i = 0; i < 32; ++i){
		// clear SC and SO
		GBA_OUT &= ~((1<<CLK_BIT) | (1<<MOSI_BIT));
		// set SO accordingly
		GBA_OUT |= ((data>>31)&1)<<MOSI_BIT;
		// set SC
		GBA_OUT |= (1<<CLK_BIT);
		// shift data
		data<<=1;
		// read SI
		data |= (GBA_IN>>MISO_BIT)&1;
	}

	sei();
	return data;
}

#define STATE_WAITING_CMD	0
#define STATE_WAITING_DATA	1
#define STATE_CMD_READY		2

int main(void) {
	clock_prescale_set(clock_div_1);

	// setup SI pin for input
	GBA_DDR &= ~(1 << MISO_BIT);
	// setup SO, SC pin for output
	GBA_DDR |= (1 << MOSI_BIT) | (1 << CLK_BIT);
	// set SC
	GBA_OUT |= (1 << CLK_BIT);

	usb_init();
	while (!usb_configured());
	_delay_ms(1000);

	unsigned int state = STATE_WAITING_CMD, cmd = 0;
	uint32_t data = 0;

	while (1) {
		switch(state){
			case STATE_WAITING_CMD:
				if(usb_serial_available() >= 1){
					cmd = usb_serial_getchar();
					if(cmd & CMD_XFER_W){
						state = STATE_WAITING_DATA;
					}else{
						state = STATE_CMD_READY;
					}
				}
				break;
			case STATE_WAITING_DATA:
				if(usb_serial_available() >= 4){
					data = 0;
					data |= (uint32_t)usb_serial_getchar()<<24;
					data |= (uint32_t)usb_serial_getchar()<<16;
					data |= (uint32_t)usb_serial_getchar()<<8;
					data |= (uint32_t)usb_serial_getchar();
					state = STATE_CMD_READY;
				}
				break;
			case STATE_CMD_READY:
				if(cmd & CMD_XFER_RW){
					data = xfer(data);
				}
				if(cmd & CMD_XFER_R){
					usb_serial_putchar((data>>24) & 0xff);
					usb_serial_putchar((data>>16) & 0xff);
					usb_serial_putchar((data>>8) & 0xff);
					usb_serial_putchar(data & 0xff);
					usb_serial_flush_output();
				}
				state = STATE_WAITING_CMD;
				break;
		}
	}
	return 0;
}

