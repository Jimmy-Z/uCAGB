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

#include "../common/common.h"

inline static void jmp_bl(void){
	// https://www.pjrc.com/teensy/jump_to_bootloader.html
	cli();
	UDCON = 1;
	USBCON = (1<<FRZCLK);  // disable USB
	UCSR1B = 0;
	_delay_ms(5);

#if defined(__AVR_ATmega32U4__)              // Teensy 2.0
	EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
	TIMSK0 = 0; TIMSK1 = 0; TIMSK3 = 0; TIMSK4 = 0; UCSR1B = 0; TWCR = 0;
	DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0; TWCR = 0;
	PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
	asm volatile("jmp 0x7E00");
#elif defined(__AVR_AT90USB1286__)             // Teensy++ 2.0
	EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
	TIMSK0 = 0; TIMSK1 = 0; TIMSK2 = 0; TIMSK3 = 0; UCSR1B = 0; TWCR = 0;
	DDRA = 0; DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0;
	PORTA = 0; PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
	asm volatile("jmp 0x1FC00");
#endif
}

#if defined(__AVR_AT90USB1286__)             // Teensy++ 2.0
#define LED_CONFIG()	(DDRD |= (1<<6))
#define LED_OFF()	(PORTD &= ~(1 << 6))
#define LED_ON()	(PORTD |= (1 << 6))
#endif

#define GBA_DDR DDRB
#define GBA_OUT PORTB
#define GBA_IN PINB
#define MOSI_BIT 2
#define MISO_BIT 3
#define CLK_BIT 1

static uint32_t data, buffer[BULK_SIZE], c_r, c_w, c_x;
static uint8_t bufpos, wait_slave;

inline static void xfer_base(void) {
	uint8_t i;
	// manipulating 32bit value in AVR could be slow
	// maybe I should expand this to 8 bit

	// gbatek says we should wait for SI(slave SO) = LOW
	// but seems like in multiboot mode the slave doesn't do this?
	if(wait_slave){
		while(GBA_IN & (1 << MISO_BIT)){
			asm("nop");
		}
	}
	for (i = 0; i < 32; ++i){
		// clear SC and SO
		GBA_OUT &= ~((1<<CLK_BIT) | (1<<MOSI_BIT));
		// set SO accordingly
		GBA_OUT |= ((data>>31)&1)<<MOSI_BIT;
		// set SC
		GBA_OUT |= (1<<CLK_BIT);
		// shift data
		data <<= 1;
		// read SI
		data |= (GBA_IN>>MISO_BIT)&1;
	}
}

inline static void xfer(void){
	cli();
	xfer_base();
	sei();
	c_x += 4;
}

inline static void xfer_bulk_ro(void){
	uint8_t i;
	cli();
	for(i = 0; i < BULK_SIZE; ++i){
		xfer_base();
		buffer[i] = data;
	}
	sei();
	c_x += (BULK_SIZE << 2);
}

inline static void xfer_bulk_wo(void){
	uint8_t i;
	cli();
	for(i = 0; i < BULK_SIZE; ++i){
		data = buffer[i];
		xfer_base();
	}
	sei();
	c_x += (BULK_SIZE << 2);
}

inline static void read_data(void){
	while(usb_serial_available() < 4){
		asm("nop");
	};
	((uint8_t*)&data)[0] = usb_serial_getchar();
	((uint8_t*)&data)[1] = usb_serial_getchar();
	((uint8_t*)&data)[2] = usb_serial_getchar();
	((uint8_t*)&data)[3] = usb_serial_getchar();
	c_r += 4;
}

inline static void read_data_bulk(void){
	uint8_t i;
	while(usb_serial_available() < (BULK_SIZE << 2)){
		asm("nop");
	};
	for(i = 0; i < (BULK_SIZE << 2); ++i){
		((uint8_t*)buffer)[i] = usb_serial_getchar();
	}
	c_r += (BULK_SIZE << 2);
}

inline static void write_data(void){
	usb_serial_write((uint8_t*)&data, 4);
	usb_serial_flush_output();
	c_w += 4;
}

inline static void write_data_bulk(void){
	usb_serial_write((uint8_t*)buffer, (BULK_SIZE << 2));
	usb_serial_flush_output();
	c_w += (BULK_SIZE << 2);
}

int main(void) {
	clock_prescale_set(clock_div_1);

	LED_CONFIG();

	// setup SI pin for input
	GBA_DDR &= ~(1 << MISO_BIT);
	// setup SO, SC pin for output
	GBA_DDR |= (1 << MOSI_BIT) | (1 << CLK_BIT);
	// set SC
	GBA_OUT |= (1 << CLK_BIT);

	usb_init();
	while(!usb_configured()){
		asm("nop");
	}
	_delay_ms(1000);

	bufpos = 0;
	wait_slave = 0;
	c_r = 0; c_w = 0; c_x = 0;

	while(1){
		while(usb_serial_available() < 1){
			asm("nop");
		}
		uint8_t cmd = usb_serial_getchar();
		uint8_t bulk = cmd & CMD_FLAG_B;
		if(cmd & CMD_FLAG_W){
			if(bulk){
				read_data_bulk();
			}else{
				read_data();
			}
		}
		switch(cmd & CMD_MASK){
			case CMD_XFER:
				if(bulk){
					if(cmd & CMD_FLAG_W){
						xfer_bulk_wo();
					}else{
						xfer_bulk_ro();
					}
				}else{
					xfer();
				}
				break;
			case CMD_PING:
				data = ~data;
				LED_ON();
				_delay_ms(10);
				LED_OFF();
				break;
			case CMD_BOOTLOADER:
				jmp_bl();
				break;
			case CMD_COUNTER:
				buffer[0] = c_r;
				buffer[1] = c_w;
				buffer[2] = c_x;
				c_r = 0; c_w = 0; c_x = 0;
				break;
			case CMD_SET_WS:
				wait_slave = 1;
				break;
			case CMD_UNSET_WS:
				wait_slave = 0;
				break;
		}
		if(cmd & CMD_FLAG_R){
			if(bulk){
				write_data_bulk();
			}else{
				write_data();
			}
		}
	}
	return 0;
}

