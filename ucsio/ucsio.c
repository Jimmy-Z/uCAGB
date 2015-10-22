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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with DFAGB. If not, see <http://www.gnu.org/licenses/>.
*/

#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "usb_serial.h"

#include "../common/common.h"

// #define TEENSY

static void cleanup(void){
	// https://www.pjrc.com/teensy/jump_to_bootloader.html
	cli();
	UDCON = 1;
	USBCON = (1<<FRZCLK);  // disable USB
	UCSR1B = 0;
	_delay_ms(5);

#if defined(__AVR_ATmega32U4__)
	EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
	TIMSK0 = 0; TIMSK1 = 0; TIMSK3 = 0; TIMSK4 = 0; UCSR1B = 0; TWCR = 0;
	DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0; TWCR = 0;
	PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
#elif defined(__AVR_AT90USB1286__)
	EIMSK = 0; PCICR = 0; SPCR = 0; ACSR = 0; EECR = 0; ADCSRA = 0;
	TIMSK0 = 0; TIMSK1 = 0; TIMSK2 = 0; TIMSK3 = 0; UCSR1B = 0; TWCR = 0;
	DDRA = 0; DDRB = 0; DDRC = 0; DDRD = 0; DDRE = 0; DDRF = 0;
	PORTA = 0; PORTB = 0; PORTC = 0; PORTD = 0; PORTE = 0; PORTF = 0;
#endif
}

inline static void jmp_bl(void){
	cleanup();
#if defined(__AVR_ATmega32U4__)
#ifdef TEENSY				// Teensy 2.0
	asm volatile("jmp 0x7E00");
#else					// Arduino/Genuino Leonardo
	asm volatile("jmp 0x7000");
#endif
#elif defined(__AVR_AT90USB1286__)	// Teensy++ 2.0
	asm volatile("jmp 0x1FC00");
#endif
}

#ifdef TEENSY		// Teensy 2.0 / Teensy++ 2.0
#define LED_CONFIG()	(DDRD |= (1<<6))
#define LED_OFF()	(PORTD &= ~(1 << 6))
#define LED_ON()	(PORTD |= (1 << 6))
#else			// Arduino/Genuino Leonardo
#define LED_CONFIG()	(DDRC |= (1 << 7))
#define LED_OFF()	(PORTC &= ~(1 << 7))
#define LED_ON()	(PORTC |= (1 << 7))
#endif

#if 1 // use PORT B
#define GBA_DDR DDRB
#define GBA_OUT PORTB
#define GBA_IN PINB
#define MOSI_BIT 2
#define MISO_BIT 3
#define CLK_BIT 1
#else // or use PORT D instead
#define GBA_DDR DDRD
#define GBA_OUT PORTD
#define GBA_IN PIND
#define MOSI_BIT 2
#define MISO_BIT 3
#define CLK_BIT 0
#endif

// Voltage Level Translation Output Enable is connected to PD1(SDA/Digital Pin 2)
#define VLTOE_DDR DDRD
#define VLTOE_OUT PORTD
#define VLTOE_BIT 1

static uint32_t data, buffer[BULK_SIZE], c_r, c_w, c_x;
static uint8_t wait_slave;

inline static void wait(){
	// gbatek says we should wait for SI(slave SO) = LOW
	// but seems like GBA doesn't do this in multiboot
	if(wait_slave){
		while(GBA_IN & (1 << MISO_BIT)){
			asm("nop");
		}
	}/*else{
	// and I tried wait for HIGH instead, doesn't work too
		while(!(GBA_IN & (1 << MISO_BIT))){
			asm("nop");
		}
	}
	*/
}

inline static void xfer(void) {
	cli();
	wait();
	for(int8_t j = 3; j >= 0; --j){
		uint8_t d8 = ((uint8_t*)&data)[j];
		for(uint8_t i = 0; i < 8; ++i){
			GBA_OUT &= ~((1<<CLK_BIT) | (1<<MOSI_BIT));
			GBA_OUT |= (d8>>7)<<MOSI_BIT;
			GBA_OUT |= (1<<CLK_BIT);
			// in my test we don't need to wait anyway
			d8 <<= 1;
			d8 |= (GBA_IN>>MISO_BIT)&1;
		}
		((uint8_t*)&data)[j] = d8;
	}
	sei();
	c_x += 4;
}

inline static void xfer_bulk(void){
	cli();
	for(uint8_t k = 0; k < BULK_SIZE; ++k){
		wait();
		for(int8_t j = 3; j >= 0; --j){
			uint8_t d8 = ((uint8_t*)&buffer[k])[j];
			for(uint8_t i = 0; i < 8; ++i){
				GBA_OUT &= ~((1<<CLK_BIT) | (1<<MOSI_BIT));
				GBA_OUT |= (d8>>7)<<MOSI_BIT;
				GBA_OUT |= (1<<CLK_BIT);
				d8 <<= 1;
				d8 |= (GBA_IN>>MISO_BIT)&1;
			}
			((uint8_t*)&buffer[k])[j] = d8;
		}
	}
	sei();
	c_x += (BULK_SIZE << 2);
}

inline static void read_data(void){
	for(uint8_t i = 0; i < 4; ++i){
		while(usb_serial_available() < 1);
		((uint8_t*)&data)[i] = usb_serial_getchar();
	}
	c_r += 4;
}

inline static void read_data_bulk(void){
	uint8_t i;
	for(uint8_t i = 0; i < (BULK_SIZE << 2); ++i){
		while(usb_serial_available() < 1);
		((uint8_t*)buffer)[i] = usb_serial_getchar();
	}
	c_r += (BULK_SIZE << 2);
}

inline static void write_data(void){
	usb_serial_write((uint8_t*)&data, 4);
	// usb_serial_flush_output();
	c_w += 4;
}

inline static void write_data_bulk(void){
	usb_serial_write((uint8_t*)buffer, (BULK_SIZE << 2));
	// usb_serial_flush_output();
	c_w += (BULK_SIZE << 2);
}

int main(void) {
	clock_prescale_set(clock_div_1);

	cleanup();

	LED_CONFIG();

	// setup SI pin for input
	GBA_DDR &= ~(1 << MISO_BIT);
	// setup SO, SC pin for output
	GBA_DDR |= (1 << MOSI_BIT) | (1 << CLK_BIT);
	// set SC
	GBA_OUT |= (1 << CLK_BIT);

	// enable VLT OE
	VLTOE_DDR |= (1 << VLTOE_BIT);
	VLTOE_OUT |= (1 << VLTOE_BIT);

	usb_init();
	while(!usb_configured()){
		_delay_ms(1);
	}

	// blinking signalling program init or watchdog reset
	// also waits for host usb
	LED_ON(); _delay_ms(6); LED_OFF();
	_delay_ms(100);
	LED_ON(); _delay_ms(6); LED_OFF();
	_delay_ms(800);
	LED_ON(); _delay_ms(6); LED_OFF();
	_delay_ms(100);
	LED_ON(); _delay_ms(6); LED_OFF();

	wdt_enable(WDTO_2S);

	wait_slave = 0;
	c_r = 0; c_w = 0; c_x = 0;

	while(1){
		while(usb_serial_available() < 1){
			wdt_reset();
		}
		wdt_reset();
		uint8_t cmd = usb_serial_getchar();
		uint8_t bulk = cmd & CMD_FLAG_B;
		if(cmd & CMD_FLAG_W){
			if(bulk){
				// LED_ON();
				read_data_bulk();
				// _delay_ms(5);
				// LED_OFF();
			}else{
				read_data();
			}
		}
		switch(cmd & CMD_MASK){
			case CMD_XFER:
				if(bulk){
					xfer_bulk();
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

