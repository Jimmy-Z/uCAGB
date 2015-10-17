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

inline static void jump_bl(void){
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

inline static uint32_t xfer(uint32_t data) {
	unsigned int i;
	LED_ON();
	cli();

	// manipulating 32bit value could be slow, maybe I shoud expand this to using 8 bit values

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

	sei();
	LED_OFF();
	return data;
}

#define STATE_WAITING_CMD	0
#define STATE_WAITING_DATA	1
#define STATE_CMD_READY		2

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
	while (!usb_configured());
	_delay_ms(1000);

	unsigned int state = STATE_WAITING_CMD, cmd = 0;
	uint32_t data = 0, counter = 0;

	while (1) {
#if 1 // single bulk mode will cause this FSM to stuck on waiting data
		switch(state){
			case STATE_WAITING_CMD:
				if(usb_serial_available() >= 1){
					cmd = usb_serial_getchar();
					if(cmd & CMD_FLAG_W){
						state = STATE_WAITING_DATA;
						LED_ON();
					}else{
						state = STATE_CMD_READY;
					}
				}
				break;
			case STATE_WAITING_DATA:
				if(usb_serial_available() >= 4){
					((uint8_t*)&data)[0] = usb_serial_getchar();
					((uint8_t*)&data)[1] = usb_serial_getchar();
					((uint8_t*)&data)[2] = usb_serial_getchar();
					((uint8_t*)&data)[3] = usb_serial_getchar();
					counter += 4;
					state = STATE_CMD_READY;
					LED_OFF();
				}
				break;
			case STATE_CMD_READY:
				if(cmd & CMD_FLAG_X){
					data = xfer(data);
				}
				if(cmd == CMD_PING){
					LED_ON();
					_delay_ms(10);
					LED_OFF();
					data = ~data;
				}else if(cmd == CMD_BOOTLOADER){
					jump_bl();
				}else if(cmd == CMD_COUNTER){
					data = counter;
					counter = 0;
				}
				if(cmd & CMD_FLAG_R){
					usb_serial_write((uint8_t*)&data, 4);
					usb_serial_flush_output();
				}
				state = STATE_WAITING_CMD;
				break;
		}
#else // single bulk mode works in this simplified reader
		if(usb_serial_available() >= 1){
			cmd = usb_serial_getchar();
			if (cmd == CMD_PING){
				data = 0x00ff55aa;
				usb_serial_write((uint8_t*)&data, 4);
				usb_serial_flush_output();
			}else if (cmd == CMD_FLAG_W){
				LED_ON();
			}else if (cmd == CMD_COUNTER){
				data = counter;
				counter = 0;
				usb_serial_write((uint8_t*)&data, 4);
				usb_serial_flush_output();
				LED_OFF();
			}else{
				++ counter;
			}
		}
#endif
	}
	// LED_ON();
	return 0;
}

