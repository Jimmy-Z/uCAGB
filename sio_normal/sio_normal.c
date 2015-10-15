/*
    This file is part of the USB-GBA multiboot project.

    The USB-GBA multiboot project is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The USB-GBA multiboot project is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the USB-GBA multiboot project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <avr/interrupt.h>
#include <avr/power.h>
#include <util/delay.h>
#include "usb_serial.h"

#define GBA_DDR DDRB
#define GBA_OUT PORTB
#define GBA_IN PINB
#define MOSI_BIT 2
#define MISO_BIT 3
#define CLK_BIT 1

int xfer(uint32_t *data_) {
    uint32_t data = *data_;
    int i;
    //WAIT_WITH_TIMEOUT(!(GBA_IN&(1<<MISO_BIT)), 25600, goto error);
    cli();

    // RESET_TIMER();
    for (i=0;i<32;++i) {
	// clear SC and SO
        GBA_OUT &= ~((1<<CLK_BIT) | (1<<MOSI_BIT));
	// set SO accordingly
        GBA_OUT |= ((data>>31)&1)<<MOSI_BIT;
        // WAIT_TIMER();
	// set SC
        GBA_OUT |= (1<<CLK_BIT);

        data<<=1;
	// read SI
        data |= (GBA_IN>>MISO_BIT)&1;
        // WAIT_TIMER();
    }

    sei();
    *data_ = data;
    return 0;
}

int main(void) {
    clock_prescale_set(clock_div_1);

    GBA_DDR &= ~(1<<MISO_BIT);
    GBA_DDR |= (1<<MOSI_BIT) | (1<<CLK_BIT);
    GBA_OUT |= (1 << CLK_BIT);

    usb_init();
    while (!usb_configured());
    _delay_ms(1000);

    // INIT_TIMER();

    while (1) {
        if (usb_serial_available() >= 5) {
            int cmd = usb_serial_getchar();
            uint32_t data = 0;
            data |= (uint32_t)usb_serial_getchar()<<24;
            data |= (uint32_t)usb_serial_getchar()<<16;
            data |= (uint32_t)usb_serial_getchar()<<8;
            data |= (uint32_t)usb_serial_getchar();
            xfer(&data);
            if(cmd){
		usb_serial_putchar((data>>24) & 0xff);
	        usb_serial_putchar((data>>16) & 0xff);
	        usb_serial_putchar((data>>8) & 0xff);
	        usb_serial_putchar(data & 0xff);
	        usb_serial_flush_output();
	    }
        }
    }
    return 0;
}

